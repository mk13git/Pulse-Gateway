#pragma once
#include <Arduino.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <vector>

// ---- Theme palette — Porkchop-style: pick a named preset, not a color wheel ----
struct Theme {
  const char *name;
  uint16_t bg;
  uint16_t primary;   // main text / icons
  uint16_t accent;    // highlights, selection, headers
};

// NOTE: colors are RGB565. Feel free to reorder/rename — nothing else in the
// firmware depends on which index is "default", just that index 0 exists.
static const Theme THEMES[] = {
  {"Cyber",  0x0000, 0xFFFF, 0x07FF}, // black / white / cyan
  {"Amber",  0x0000, 0xFD20, 0xFBE0}, // black / amber / warm amber
  {"Mint",   0x0000, 0xFFFF, 0x07E6}, // black / white / mint
  {"Rose",   0x0000, 0xFFFF, 0xF81F}, // black / white / magenta-rose
  {"Mono",   0x0000, 0xFFFF, 0x8410}, // black / white / grey
  {"Sunset", 0x0000, 0xFFFF, 0xFB20}, // black / white / orange
};
static const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

// ---- Agents ----
// Fixed set for now (matches the swarm: Claude Code, Gemini CLI, Codex, Grok).
// Everything else about an agent (auth, routing) lives server-side — the
// device only ever stores a display color + on/off, per the "server owns
// auth" design.
static const char *AGENT_NAMES[] = {"Claude", "Gemini", "Codex", "Grok"};
static const int AGENT_COUNT = 4;

// Small fixed swatch for the agent color picker — indices only, so storage
// is one byte per agent instead of a packed RGB565 value.
struct AgentColorSwatch {
  const char *name;
  uint16_t color;
};
static const AgentColorSwatch AGENT_COLOR_SWATCHES[] = {
  {"Red",    0xF800},
  {"Orange", 0xFB20},
  {"Yellow", 0xFFE0},
  {"Green",  0x07E0},
  {"Cyan",   0x07FF},
  {"Blue",   0x001F},
  {"Purple", 0x8010},
  {"Pink",   0xF81F},
};
static const int AGENT_SWATCH_COUNT = sizeof(AGENT_COLOR_SWATCHES) / sizeof(AGENT_COLOR_SWATCHES[0]);

// Live per-agent state, as last reported by GET /api/gateway/agents.
// NOTE: assumed response shape is [{name, state}] where state is one of
// "idle" | "queued" | "working" — adjust parseAgentState() in main.cpp if
// the real endpoint differs.
enum AgentActivity { AGENT_IDLE, AGENT_QUEUED, AGENT_WORKING };

struct Settings {
  String wifiSsid = "";
  String wifiPassword = "";
  int themeIndex = 0;
  int brightnessPct = 80; // 10-100, steps of 10
  int volumePct = 60;     // 0-100, steps of 10

  // Server connection — device always reaches out, never gets dialed in to,
  // so this is just "which address do I poll/POST right now".
  String localAddress = "";      // e.g. 192.168.1.42:3000
  String tailscaleAddress = "";  // e.g. 100.x.x.x:3000
  int activeConnection = 0;      // 0 = local (green dot), 1 = tailscale (blue dot)

  // Per-agent: enabled (routable) + display color swatch index.
  bool agentEnabled[AGENT_COUNT] = {true, true, true, true};
  int agentColorIdx[AGENT_COUNT] = {0, 3, 5, 1}; // Red/Green/Blue/Orange defaults

  // Recordings: sequential MEMO-##### numbering, never reused.
  uint32_t nextMemoSeq = 1;
};

extern Settings gSettings;

void settingsLoad();
void settingsSave();
void settingsApplyDisplayAndAudio(); // brightness + volume + theme, call after any change
bool settingsHasWifi();

// Builds "http://<active address>" from local/tailscale + activeConnection.
// Falls back to PULSE_BASE_URL (config.h) if neither field has been set yet.
String settingsActiveBaseUrl();

// Full settings menu (WiFi / Theme / Brightness / Volume / Agents / Back).
// Blocks until the user backs out with ESC. Call from the home screen.
void settingsRunMenu();

// WiFi-only setup screen (scan -> pick -> type password). Used both from the
// menu and directly on first boot when no WiFi is saved yet.
void settingsRunWifiSetup();

// ---- Shared input/drawing helpers (used by settings.cpp, recordings.cpp, main.cpp) ----
//
// NOTE on key mapping: the Cardputer ADV keyboard has no dedicated arrow
// keys. This firmware uses the punctuation-as-arrows convention confirmed
// against the physical unit: ',' = left, '/' = right, ';' = up, '.' = down.
// ESC (dedicated top-left key) is the universal "back one level" key
// everywhere in the UI — it never jumps straight to the home screen, it
// only pops one level of the screen stack. DEL is used for its literal
// meaning (character backspace in text entry, or delete-item in menus).
struct KeyEvent {
  bool left = false, right = false, up = false, down = false;
  bool enter = false;   // ok key
  bool back = false;    // del key — literal backspace/delete
  bool escape = false;  // esc key — always "back one level"
  String typed = "";    // any other printable chars this tick
};

KeyEvent pollKeys();
void drawHeader(const char *title);
String textInputScreen(const char *prompt, bool mask);