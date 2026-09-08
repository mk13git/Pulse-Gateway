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
  f.wr