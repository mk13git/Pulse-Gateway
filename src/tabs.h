#pragma once
#include <Arduino.h>

// Each of these is a blocking screen: fetches once on entry, lets you
// browse/act, ESC backs out one level. None auto-refresh while open —
// press 'r' to re-fetch if something changed server-side.

// Per-agent usage vs cap — GET /api/gateway/agents.
// NOTE: assumes each agent object carries usage info. Written against
// either a direct "usagePct" (0-100) field or "used"/"cap" numbers,
// whichever the real response has — adjust usageRunScreen() in tabs.cpp
// to match once you check the actual shape.
void usageRunScreen();

// Tasks -> Active / Queued / Finished submenu. Each list filters (client
// side, for now) the same GET /api/gateway/tasks response by status.
// NOTE: this endpoint does not exist yet server-side — see the comment in
// tabs.cpp. Screens fail gracefully to a message until it's added.
void tasksMenuScreen();

// Open handoffs list -> ENTER opens a reroute popup listing every
// connected agent, with whichever agent currently holds that task greyed
// out (not selectable) and the server's recommended agent marked.
// Confirms via POST /api/gateway/handoffs/reroute { handoffId, agentName }.
//
// If preselectHandoffId is non-empty, the screen opens straight onto that
// handoff with the reroute popup already up — used for the cap-warning
// alert flow (see main.cpp), so one ENTER on your pick is all it takes.
//
// NOTE: the GET already exists; the reroute POST, the recommendedAgent
// field, and the currentAgent field are all unconfirmed against the real
// API shape — adjust once wired up server-side.
void handoffRunScreen(const String &preselectHandoffId = "");