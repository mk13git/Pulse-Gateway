// Pulse Gateway firmware — M5Stack Cardputer ADV
//
// Home screen: idle-animated sine waveform (goes live while recording),
// recording timer, battery % + connection-color dot (green=local,
// blue=Tailscale), small per-agent activity dots. Tab bar underneath:
// Favorites / Tasks / Handoff / Settings / Recordings.
//
// Control key: hold = record, send on release. Two quick taps = toggle
// hands-free recording on; one more tap = stop and prompt Save/Send.
//
// ESC is the universal "back one level" key everywhere — never a jump
// straight to home. See settings.h for the key-mapping convention.
//
// Written and typechecked against the M5Unified/M5Cardputer APIs but not
// yet run on real hardware for this pass. Favorites and Tasks talk to
// endpoints assumed on the Pulse web app (/agents field names, and a new
// /api/gateway/tasks endpoint that doesn't exist server-side yet — see
// tabs.h). Handoff's reroute POST is similarly unconfirmed against the
// real API. Places most likely to need adjustment are marked NOTE:.

#include <M5Cardputer.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "config.h"
#include "wav.h"
#include "settings.h"
#include "recordings.h"
#include "tabs.h"
#include "net.h"

M5Canvas canvas(&M5Cardputer.Display);

// ---- Tabs ----
enum Tab { TAB_TASKS, TAB_RECORDINGS, TAB_HANDOFF, TAB_USAGE, TAB_SETTINGS, TAB_COUNT };
static const char *TAB_NAMES[] = {"Tasks", "Rec.", "Handoff", "Usage", "Settings"};
int selectedTab = 0;
String pendingHandoffId = ""; // set by a cap-warning signal, consumed at the top of loop()

// ---- State ----
String lastSeenSignalId = "";
unsigned long lastPollAt = 0;
AgentActivity gAgentState[AGENT_COUNT] = {AGENT_IDLE, AGENT_IDLE, AGENT_IDLE, AGENT_IDLE};

// Recording capture buffer (shared by hold + toggle modes)
bool recording = false;
int16_t *recordBuffer = nullptr;
size_t recordCapacitySamples = 0;
size_t recordedSamples = 0;
unsigned long recordStartedAt = 0;

// Control-key hold/double-tap state machine
enum RecMode { REC_NONE, REC_HOLD, REC_TOGGLE };
RecMode recMode = REC_NONE;
bool ctrlWasDown = false;
unsigned long ctrlDownAt = 0;
unsigned long lastTapAt = 0; // 0 = no pending tap

// Idle waveform animation phase
float wavePhase = 0;

// ---- Small drawing helpers ----

uint16_t connectionDotColor() {
  return gSettings.activeConnection == 1 ? 0x001F /*blue*/ : 0x07E0 /*green*/;
}

void drawTopRow() {
  const Theme &t = THEMES[gSettings.themeIndex];
  int batteryPct = M5Cardputer.Power.getBatteryLevel();
  canvas.setTextColor(t.primary);
  canvas.setTextSize(1);
  canvas.setCursor(4, 4);
  canvas.printf("%d%%", batteryPct);
  canvas.fillCircle(canvas.getCursorX() + 6, 8, 3, connectionDotColor());

  // Per-agent activity dots, top-right. Idle agents are simply omitted.
  int x = 236;
  for (int i = AGENT_COUNT - 1; i >= 0; i--) {
    if (!gSettings.agentEnabled[i]) continue;
    if (gAgentState[i] == AGENT_IDLE) continue;
    bool visible = true;
    if (gAgentState[i] == AGENT_WORKING) visible = (millis() / 500) % 2 == 0; // slow blink
    if (visible) {
      canvas.fillCircle(x, 8, 3, AGENT_COLOR_SWATCHES[gSettings.agentColorIdx[i]].color);
    }
    x -= 10;
  }
}

void drawWaveform(int x, int y, int w, int h) {
  const Theme &t = THEMES[gSettings.themeIndex];
  int midY = y + h / 2;
  int prevX = x, prevY = midY;
  for (int i = 0; i < w; i++) {
    int px = x + i;
    int py;
    if (recording) {
      // Live: pull amplitude from the most recently captured samples,
      // mapped across the width of the plot.
      size_t sampleIdx = recordedSamples > (size_t)w
        ? recordedSamples - w + i
        : (recordedSamples > 0 ? (i * recordedSamples) / w : 0);
      int16_t s = (recordedSamples > 0 && sampleIdx < recordedSamples) ? recordBuffer[sampleIdx] : 0;
      py = midY - (int)(((float)s / 32768.0f) * (h / 2));
    } else {
      // Idle: gentle continuous sine, scrolling left-to-right via wavePhase.
      float angle = (i * 0.15f) + wavePhase;
      py = midY - (int)(sinf(angle) * (h / 4));
    }
    if (i > 0) canvas.drawLine(prevX, prevY, px, py, t.accent);
    prevX = px; prevY = py;
  }
}

void drawRecordingTimer() {
  if (!recording) return;
  const Theme &t = THEMES[gSettings.themeIndex];
  unsigned long secs = (millis() - recordStartedAt) / 1000;
  canvas.setTextColor(t.accent);
  canvas.setTextSize(1);
  canvas.setCursor(100, 96);
  canvas.printf("%02lu:%02lu", secs / 60, secs % 60);
}

void drawTabBar() {
  const Theme &t = THEMES[gSettings.themeIndex];
  int tabW = 240 / TAB_COUNT;
  int pad = 2, r = 5, barY = 110, barH = 25;
  for (int i = 0; i < TAB_COUNT; i++) {
    int x = i * tabW + pad, w = tabW - pad * 2;
    if (i == selectedTab) {
      canvas.fillRoundRect(x, barY + pad, w, barH - pad * 2, r, t.accent);
      canvas.setTextColor(t.bg);
    } else {
      canvas.drawRoundRect(x, barY + pad, w, barH - pad * 2, r, t.accent);
      canvas.setTextColor(t.primary);
    }
    canvas.setTextSize(0.8); // a touch smaller than the rest of the UI so "Settings" fits with room to spare
    int textW = canvas.textWidth(TAB_NAMES[i]);
    canvas.setCursor(x + (w - textW) / 2, barY + pad + 7);
    canvas.print(TAB_NAMES[i]);
  }
}

void drawHome() {
  const Theme &t = THEMES[gSettings.themeIndex];
  canvas.fillScreen(t.bg);
  drawTopRow();
  drawWaveform(0, 16, 240, 76);
  drawRecordingTimer();
  drawTabBar();
  canvas.pushSprite(0, 0);
}

void drawSimpleStatus(const String &title, const String &body, uint16_t color) {
  const Theme &t = THEMES[gSettings.themeIndex];
  canvas.fillScreen(t.bg);
  canvas.setTextColor(color);
  canvas.setTextSize(2);
  canvas.setCursor(4, 20);
  canvas.print(title);
  canvas.setTextColor(t.primary);
  canvas.setTextSize(1);
  canvas.setCursor(4, 48);
  int lineLen = 34, i = 0;
  while (i < (int)body.length() && canvas.getCursorY() < 130) {
    canvas.println(body.substring(i, i + lineLen));
    i += lineLen;
  }
  canvas.pushSprite(0, 0);
}

// ---- Signal + agent polling ----

// NOTE: "cap_warning" is a new signal kind, not in the original
// SIGNAL_KINDS list (complete/approval/queued/handoff) — needs adding
// server-side, fired once an agent crosses ~90% of its usage cap (see
// usageColor() in tabs.cpp for where that threshold is mirrored on-device).
// Assumed payload: { id, kind: "cap_warning", handoffId, agentName }.
void pollSignals() {
  JsonDocument doc;
  if (!httpGetJson("/api/gateway/signals?limit=5", doc)) return;
  JsonArray signals = doc["signals"].as<JsonArray>();
  if (signals.size() == 0) return;

  JsonObject latest = signals[0];
  String id = latest["id"].as<String>();
  if (id == lastSeenSignalId) return;
  lastSeenSignalId = id;

  String kind = latest["kind"].as<String>();
  if (kind == "cap_warning") {
    // Distinct triple-beep, then jump straight to Handoff with the popup
    // already open on that task — see handleTab()/loop() below.
    M5Cardputer.Speaker.tone(2200, 90); delay(60);
    M5Cardputer.Speaker.tone(2200, 90); delay(60);
    M5Cardputer.Speaker.tone(2200, 90);
    pendingHandoffId = latest["handoffId"].as<String>();
    return;
  }

  int freq = kind == "approval" ? 1800 : kind == "handoff" ? 900 : kind == "voice_task" ? 1400 : 1200;
  M5Cardputer.Speaker.tone(freq, 120);
}

// NOTE: assumed response shape { "agents": [ { "name": "Claude", "state": "idle" }, ... ] }
// from GET /api/gateway/agents — adjust the field names below if the real
// endpoint differs once you check it against the actual Gateway API.
void pollAgents() {
  JsonDocument doc;
  if (!httpGetJson("/api/gateway/agents", doc)) return;
  JsonArray agents = doc["agents"].as<JsonArray>();
  for (JsonObject a : agents) {
    String name = a["name"].as<String>();
    String state = a["state"].as<String>();
    for (int i = 0; i < AGENT_COUNT; i++) {
      if (name.equalsIgnoreCase(AGENT_NAMES[i])) {
        gAgentState[i] = state == "working" ? AGENT_WORKING : state == "queued" ? AGENT_QUEUED : AGENT_IDLE;
      }
    }
  }
}

// ---- Voice recording ----

void startRecording() {
  if (!psramFound()) return;
  recordCapacitySamples = MIC_SAMPLE_RATE * MAX_RECORD_SECONDS;
  recordBuffer = (int16_t *)ps_malloc(recordCapacitySamples * sizeof(int16_t));
  if (!recordBuffer) return;
  recordedSamples = 0;
  recording = true;
  recordStartedAt = millis();
  M5Cardputer.Mic.begin();
}

// Pulls whatever samples are available this tick. Call every loop while recording.
void pumpRecording() {
  if (!recording) return;
  size_t room = recordCapacitySamples - recordedSamples;
  if (room > 0) {
    size_t got = M5Cardputer.Mic.record(recordBuffer + recordedSamples, room, MIC_SAMPLE_RATE);
    recordedSamples += got;
  }
  if (millis() - recordStartedAt > (unsigned long)MAX_RECORD_SECONDS * 1000) {
    // Safety cap — treated as a HOLD-style send-immediately in the caller.
  }
}

// Discards a too-short probe capture (used when a tap turns out to be a
// single tap, not the start of a double-tap or a real hold).
void abortRecording() {
  recording = false;
  M5Cardputer.Mic.end();
  if (recordBuffer) { free(recordBuffer); recordBuffer = nullptr; }
}

// HOLD path: always sends immediately. Per Martin: short hold-messages
// aren't important enough to save if sending fails outright.
void stopRecordingAndSend() {
  recording = false;
  M5Cardputer.Mic.end();

  if (recordedSamples < MIC_SAMPLE_RATE / 2) {
    abortRecording();
    return;
  }

  drawSimpleStatus("Sending...", "transcribing on the server", 0x07FF);
  uint32_t dataBytes = recordedSamples * 2;
  uint8_t *wav = (uint8_t *)ps_malloc(WAV_HEADER_SIZE + dataBytes);
  if (!wav) { free(recordBuffer); recordBuffer = nullptr; return; }
  writeWavHeader(wav, dataBytes, MIC_SAMPLE_RATE);
  memcpy(wav + WAV_HEADER_SIZE, recordBuffer, dataBytes);
  free(recordBuffer);
  recordBuffer = nullptr;

  JsonDocument resp;
  bool ok = uploadWavBuffer(wav, WAV_HEADER_SIZE + dataBytes, resp);
  free(wav);

  if (ok && resp["ok"] == true) {
    String transcript = resp["transcript"].as<String>();
    drawSimpleStatus("Queued", transcript, 0x07E0);
    M5Cardputer.Speaker.tone(1600, 100);
    delay(600);
  } else {
    drawSimpleStatus("Send failed", "no connection - not saved", 0xF800);
    M5Cardputer.Speaker.tone(400, 200);
    delay(800);
  }
}

// Blocking Save/Send prompt for the TOGGLE path. ESC defaults to Save so a
// recording is never silently lost.
void saveOrSendPrompt() {
  bool wifiUp = WiFi.status() == WL_CONNECTED;
  int sel = 0; // 0 = Save, 1 = Send (Send hidden/skipped if no WiFi)
  while (true) {
    const Theme &t = THEMES[gSettings.themeIndex];
    drawHeader("Recording done");
    canvas.setCursor(4, 30);
    canvas.setTextColor(sel == 0 ? t.accent : t.primary);
    canvas.println("Save to SD");
    if (wifiUp) {
      canvas.setCursor(4, 44);
      canvas.setTextColor(sel == 1 ? t.accent : t.primary);
      canvas.println("Send now");
    } else {
      canvas.setCursor(4, 44);
      canvas.setTextColor(t.primary);
      canvas.println("(Send needs a connection)");
    }
    canvas.setCursor(4, 110);
    canvas.setTextColor(t.primary);
    canvas.print(",/= pick  ENTER=ok  ESC=save");
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (ev.left || ev.right) sel = wifiUp ? (sel == 0 ? 1 : 0) : 0;
    if (ev.escape) { sel = 0; break; }
    if (ev.enter) break;
    delay(30);
  }

  uint32_t dataBytes = recordedSamples * 2;
  if (sel == 1 && wifiUp) {
    uint8_t *wav = (uint8_t *)ps_malloc(WAV_HEADER_SIZE + dataBytes);
    if (wav) {
      writeWavHeader(wav, dataBytes, MIC_SAMPLE_RATE);
      memcpy(wav + WAV_HEADER_SIZE, recordBuffer, dataBytes);
      JsonDocument resp;
      bool ok = uploadWavBuffer(wav, WAV_HEADER_SIZE + dataBytes, resp);
      free(wav);
      drawSimpleStatus(ok ? "Sent" : "Send failed", "", ok ? 0x07E0 : 0xF800);
      delay(500);
    }
  } else {
    int idx = recordingsCreate(recordBuffer, recordedSamples);
    drawSimpleStatus(idx >= 0 ? "Saved" : "Save failed", "", idx >= 0 ? 0xFFE0 : 0xF800);
    delay(500);
  }
  free(recordBuffer);
  recordBuffer = nullptr;
}

void stopRecordingAndOfferSaveSend() {
  recording = false;
  M5Cardputer.Mic.end();
  if (recordedSamples < MIC_SAMPLE_RATE / 2) {
    abortRecording();
    return;
  }
  saveOrSendPrompt();
}

// ---- Control key: hold-to-record vs. double-tap toggle ----

void handleControlKey() {
  bool ctrlDown = M5Cardputer.Keyboard.isKeyPressed(KEY_LEFT_CTRL); // NOTE: verify constant name for this lib version

  if (ctrlDown && !ctrlWasDown) {
    if (recMode == REC_TOGGLE) {
      stopRecordingAndOfferSaveSend();
      recMode = REC_NONE;
      lastTapAt = 0;
    } else if (recMode == REC_NONE) {
      recMode = REC_HOLD; // tentative — reclassified on release
      startRecording();
    }
    ctrlDownAt = millis();
  }

  if (!ctrlDown && ctrlWasDown && recMode == REC_HOLD) {
    unsigned long heldFor = millis() - ctrlDownAt;
    if (heldFor < TAP_THRESHOLD_MS) {
      if (lastTapAt != 0 && millis() - lastTapAt < DOUBLE_TAP_WINDOW_MS) {
        recMode = REC_TOGGLE; // second tap of a double — keep recording, hands-free
        lastTapAt = 0;
      } else {
        abortRecording(); // lone short tap — discard, wait to see if a second follows
        recMode = REC_NONE;
        lastTapAt = millis();
      }
    } else {
      stopRecordingAndSend(); // genuine hold
      recMode = REC_NONE;
      lastTapAt = 0;
    }
  }

  ctrlWasDown = ctrlDown;
}

// ---- Setup / loop ----

void connectWiFi() {
  if (!settingsHasWifi()) {
    settingsRunWifiSetup();
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(gSettings.wifiSsid.c_str(), gSettings.wifiPassword.c_str());
  drawSimpleStatus("Connecting", gSettings.wifiSsid, 0x07FF);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    drawSimpleStatus("WiFi failed", "check Settings", 0xF800);
    delay(1000);
  }
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);
  canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
  canvas.setTextWrap(false);

  settingsLoad();
  settingsApplyDisplayAndAudio();
  recordingsInit();
  connectWiFi();
}

void loop() {
  M5Cardputer.update();

  // Cap-warning alert: jump straight to Handoff with that task's reroute
  // popup already open, ahead of normal tab navigation.
  if (pendingHandoffId.length() > 0 && !recording) {
    selectedTab = TAB_HANDOFF;
    String id = pendingHandoffId;
    pendingHandoffId = "";
    handoffRunScreen(id);
    return;
  }

  // Control key is only live at the home screen — submenu screens
  // (settings, recordings) own the keyboard fully while they're running.
  if (!recording) {
    // Left/right pick a tab, Enter opens it — only when not mid-recording.
    KeyEvent ev = pollKeys();
    if (ev.left) selectedTab = (selectedTab - 1 + TAB_COUNT) % TAB_COUNT;
    if (ev.right) selectedTab = (selectedTab + 1) % TAB_COUNT;
    if (ev.enter) {
      switch (selectedTab) {
        case TAB_TASKS: tasksMenuScreen(); break;
        case TAB_RECORDINGS: recordingsRunScreen(); break;
        case TAB_HANDOFF: handoffRunScreen(); break;
        case TAB_USAGE: usageRunScreen(); break;
        case TAB_SETTINGS:
          settingsRunMenu();
          settingsApplyDisplayAndAudio();
          if (WiFi.status() != WL_CONNECTED) connectWiFi();
          break;
      }
      return; // redraw home fresh next tick
    }
  }

  handleControlKey();

  if (recording) {
    pumpRecording();
    if (millis() - recordStartedAt > (unsigned long)MAX_RECORD_SECONDS * 1000) {
      if (recMode == REC_TOGGLE) stopRecordingAndOfferSaveSend();
      else stopRecordingAndSend();
      recMode = REC_NONE;
    }
  }

  wavePhase += 0.12f; // idle waveform scroll speed — tune to taste

  unsigned long now = millis();
  if (now - lastPollAt > POLL_INTERVAL_MS && WiFi.status() == WL_CONNECTED) {
    lastPollAt = now;
    pollSignals();
    pollAgents();
  }

  drawHome();
  delay(20);
}