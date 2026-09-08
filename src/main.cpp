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
AgentActivity g