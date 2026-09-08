#include "recordings.h"
#include "settings.h"
#include "net.h"
#include "wav.h"
#include "config.h"
#include <M5Cardputer.h>
#include <M5GFX.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <WiFi.h>

extern M5Canvas canvas; // shared with main.cpp

static const char *RECORDINGS_DIR = "/recordings";
static const char *INDEX_PATH = "/recordings/index.json";

static std::vector<RecordingEntry> gRecordings;
static bool gSdReady = false;

static bool gPlaying = false;
static uint8_t *gPlayBuf = nullptr;

// ---- Index persistence ----

static void loadIndex() {
  gRecordings.clear();
  File f = SD.open(INDEX_PATH, FILE_READ);
  if (!f) return;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;
  for (JsonObject o : doc.as<JsonArray>()) {
    RecordingEntry e;
    e.seq = o["seq"] | 0;
    e.label = o["label"] | "";
    e.sent = o["sent"] | false;
    gRecordings.push_back(e);
  }
}

static void saveIndex() {
  if (!gSdReady) return;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (auto &e : gRecordings) {
    JsonObject o = arr.add<JsonObject>();
    o["seq"] = e.seq;
    o["label"] = e.label;
    o["sent"] = e.sent;
  }
  File f = SD.open(INDEX_PATH, FILE_WRITE);
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

static String wavPathFor(uint32_t seq) {
  return String(RECORDINGS_DIR) + "/" + seq + ".wav";
}

// ---- Init ----

void recordingsInit() {
  // NOTE: M5Unified auto-configures the SD SPI pins for known Cardputer
  // boards, so a plain SD.begin() is usually enough. If it fails to mount
  // on the real ADV unit, this is the spot to pass an explicit CS pin.
  gSdReady = SD.begin();
  if (!gSdReady) return;
  if (!SD.exists(RECORDINGS_DIR)) SD.mkdir(RECORDINGS_DIR);
  loadIndex();
}

bool recordingsReady() { return gSdReady; }

std::vector<RecordingEntry> &recordingsList() { return gRecordings; }

// ---- Create / send / rename / delete ----

int recordingsCreate(const int16_t *samples, size_t sampleCount) {
  if (!gSdReady) return -1;

  uint32_t seq = gSettings.nextMemoSeq++;
  settingsSave(); // persist the counter immediately so a crash can't reuse a number

  uint32_t dataBytes = sampleCount * 2;
  File f = SD.open(wavPathFor(seq), FILE_WRITE);
  if (!f) return -1;
  uint8_t header[WAV_HEADER_SIZE];
  writeWavHeader(header, dataBytes, MIC_SAMPLE_RATE);
  f.write(header, WAV_HEADER_SIZE);
  f.write((const uint8_t *)samples, dataBytes);
  f.close();

  RecordingEntry e;
  e.seq = seq;
  char label[16];
  snprintf(label, sizeof(label), "MEMO-%05u", seq);
  e.label = label;
  e.sent = false;
  gRecordings.push_back(e);
  saveIndex();
  return (int)gRecordings.size() - 1;
}

bool recordingsSend(int idx) {
  if (idx < 0 || idx >= (int)gRecordings.size()) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  File f = SD.open(wavPathFor(gRecordings[idx].seq), FILE_READ);
  if (!f) return false;
  size_t len = f.size();
  uint8_t *buf = (uint8_t *)ps_malloc(len);
  if (!buf) { f.close(); return false; }
  f.read(buf, len);
  f.close();

  JsonDocument resp;
  bool ok = uploadWavBuffer(buf, len, resp);
  free(buf);

  if (ok && resp["ok"] == true) {
    gRecordings[idx].sent = true;
    saveIndex();
    return true;
  }
  return false;
}

void recordingsRename(int idx, const String &newLabel) {
  if (idx < 0 || idx >= (int)gRecordings.size()) return;
  if (newLabel.length() == 0) return;
  gRecordings[idx].label = newLabel; // full replace — no MEMO number kept, per Martin
  saveIndex();
}

void recordingsDelete(int idx) {
  if (idx < 0 || idx >= (int)gRecordings.size()) return;
  SD.remove(wavPathFor(gRecordings[idx].seq));
  gRecordings.erase(gRecordings.begin() + idx);
  saveIndex();
}

// ---- Playback ----

void recordingsPause() {
  if (!gPlaying) return;
  M5Cardputer.Speaker.stop();
  gPlaying = false;
  if (gPlayBuf) { free(gPlayBuf); gPlayBuf = nullptr; }
}

void recordingsPlay(int idx) {
  if (idx < 0 || idx >= (int)gRecordings.size()) return;
  recordingsPause(); // stop anything already playing first

  File f = SD.open(wavPathFor(gRecordings[idx].seq), FILE_READ);
  if (!f) return;
  size_t len = f.size();
  gPlayBuf = (uint8_t *)ps_malloc(len);
  if (!gPlayBuf) { f.close(); return; }
  f.read(gPlayBuf, len);
  f.close();

  // Skip the 44-byte WAV header, play raw PCM16 mono at the recorded rate.
  // NOTE: M5Unified's Speaker::playRaw() argument order has shifted across
  // versions (repeat count / stereo flag position) — check this call
  // against your installed version if it doesn't compile as-is.
  M5Cardputer.Speaker.playRaw((const int16_t *)(gPlayBuf + WAV_HEADER_SIZE),
                               (len - WAV_HEADER_SIZE) / 2, MIC_SAMPLE_RATE, false);
  gPlaying = true;
}

// ---- Recordings tab screen ----

static void drawRecordingRow(int i, int y, bool selected) {
  const Theme &t = THEMES[gSettings.themeIndex];
  RecordingEntry &e = gRecordings[i];
  canvas.fillCircle(10, y + 4, 4, e.sent ? 0x07E0 /*green*/ : 0xFFE0 /*yellow*/);
  canvas.setCursor(22, y);
  canvas.setTextColor(selected ? t.accent : t.primary);
  canvas.println(e.label);
}

// Returns the chosen action index, or -1 if ESC was pressed (back to list).
static int recordingPopupMenu(int idx) {
  const char *items[] = {"Play", "Pause", "Send", "Rename", "Delete"};
  int sel = 0;
  while (true) {
    const Theme &t = THEMES[gSettings.themeIndex];
    drawHeader(gRecordings[idx].label.c_str());
    for (int i = 0; i < 5; i++) {
      canvas.setCursor(4, 30 + i * 14);
      canvas.setTextColor(i == sel ? t.accent : t.primary);
      canvas.println(items[i]);
    }
    canvas.setCursor(4, 110);
    canvas.print(";/.=pick ENTER=select ESC=back");
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (ev.up) sel = (sel - 1 + 5) % 5;
    if (ev.down) sel = (sel + 1) % 5;
    if (ev.escape) return -1;
    if (ev.enter) return sel;
    delay(30);
  }
}

void recordingsRunScreen() {
  int sel = 0;
  while (true) {
    const Theme &t = THEMES[gSettings.themeIndex];
    drawHeader("Recordings");

    if (!gSdReady) {
      canvas.setCursor(4, 30);
      canvas.println("No SD card found.");
      canvas.pushSprite(0, 0);
      KeyEvent ev = pollKeys();
      if (ev.escape) return;
      delay(30);
      continue;
    }

    if (gRecordings.empty()) {
      canvas.setCursor(4, 30);
      canvas.println("No recordings yet.");
    } else {
      for (size_t i = 0; i < gRecordings.size() && i < 7; i++) {
        drawRecordingRow((int)i, 26 + (int)i * 13, (int)i == sel);
      }
    }
    canvas.setCursor(4, 112);
    canvas.setTextColor(t.primary);
    canvas.print(";/.=pick ENTER=menu ESC=back");
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (!gRecordings.empty()) {
      if (ev.up) sel = (sel - 1 + (int)gRecordings.size()) % (int)gRecordings.size();
      if (ev.down) sel = (sel + 1) % (int)gRecordings.size();
      if (ev.enter) {
        int action = recordingPopupMenu(sel);
        switch (action) {
          case 0: recordingsPlay(sel); break;
          case 1: recordingsPause(); break;
          case 2: recordingsSend(sel); break;
          case 3: {
            String newLabel = textInputScreen("New name:", false);
            if (newLabel.length() > 0) recordingsRename(sel, newLabel);
            break;
          }
          case 4:
            recordingsDelete(sel);
            if (sel >= (int)gRecordings.size()) sel = max(0, (int)gRecordings.size() - 1);
            break;
          default: break; // -1 = ESC out of popup, just back to the list
        }
      }
    }
    if (ev.escape) { recordingsPause(); return; } // back to tab bar / caller
    delay(30);
  }
}
