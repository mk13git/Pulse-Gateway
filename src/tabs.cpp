#include "tabs.h"
#include "settings.h"
#include "net.h"
#include <M5Cardputer.h>
#include <M5GFX.h>
#include <ArduinoJson.h>
#include <vector>

extern M5Canvas canvas; // shared with main.cpp

// ---- Usage: per-agent cap usage, reuses GET /api/gateway/agents ----

struct UsageRow { String name; int pct; }; // pct = 0-100, how close to cap

static uint16_t usageColor(const Theme &t, int pct) {
  if (pct >= 90) return 0xF800; // red — this is the same threshold that
                                 // should trigger the cap-warning alert
                                 // server-side (see handoffRunScreen note)
  if (pct >= 70) return 0xFFE0; // yellow
  return t.accent;
}

void usageRunScreen() {
  std::vector<UsageRow> rows;
  bool failed = false;

  auto fetch = [&]() {
    JsonDocument doc;
    failed = !httpGetJson("/api/gateway/agents", doc);
    rows.clear();
    if (!failed) {
      for (JsonObject a : doc["agents"].as<JsonArray>()) {
        UsageRow r;
        r.name = a["name"].as<String>();
        // NOTE: field names assumed — this firmware tries a direct
        // "usagePct" first, then falls back to computing it from
        // "used"/"cap" if that's what the real response has instead.
        if (a["usagePct"].is<int>()) {
          r.pct = a["usagePct"];
        } else {
          int used = a["used"] | 0, cap = a["cap"] | 0;
          r.pct = cap > 0 ? (used * 100) / cap : 0;
        }
        rows.push_back(r);
      }
    }
  };
  fetch();

  while (true) {
    const Theme &t = THEMES[gSettings.themeIndex];
    drawHeader("Usage");
    if (failed) {
      canvas.setCursor(4, 30);
      canvas.println("Couldn't reach the Gateway.");
    } else if (rows.empty()) {
      canvas.setCursor(4, 30);
      canvas.println("No agents reporting yet.");
    } else {
      for (size_t i = 0; i < rows.size() && i < 4; i++) {
        int y = 28 + i * 20;
        canvas.setTextColor(t.primary);
        canvas.setCursor(4, y);
        canvas.printf("%s  %d%%", rows[i].name.c_str(), rows[i].pct);
        int barW = 220, barX = 4, barY = y + 11;
        canvas.drawRect(barX, barY, barW, 6, t.primary);
        canvas.fillRect(barX, barY, (barW * min(rows[i].pct, 100)) / 100, 6, usageColor(t, rows[i].pct));
      }
    }
    canvas.setCursor(4, 112);
    canvas.setTextColor(t.primary);
    canvas.print("r=refresh  ESC=back");
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (ev.typed.indexOf('r') >= 0) fetch();
    if (ev.escape) return;
    delay(30);
  }
}

// ---- Tasks -> Active / Queued / Finished ----
// NOTE: GET /api/gateway/tasks doesn't exist server-side yet. It needs to
// return [{id, title, status: "active"|"queued"|"done"}] merging whatever
// the app already tracks. Filtering by status happens client-side here
// against one fetch — fine at this scale, revisit if the list ever gets
// large enough to want separate endpoints per status.

struct TaskRow { String title; String status; };

static void tasksListScreen(const char *title, const char *statusFilter) {
  std::vector<TaskRow> rows;
  bool failed = false;

  auto fetch = [&]() {
    JsonDocument doc;
    failed = !httpGetJson("/api/gateway/tasks", doc);
    rows.clear();
    if (!failed) {
      for (JsonObject o : doc["tasks"].as<JsonArray>()) {
        String status = o["status"].as<String>();
        if (status == statusFilter) {
          TaskRow r;
          r.title = o["title"].as<String>();
          r.status = status;
          rows.push_back(r);
        }
      }
    }
  };
  fetch();

  int sel = 0, scroll = 0;
  while (true) {
    const Theme &t = THEMES[gSettings.themeIndex];
    drawHeader(title);
    if (failed) {
      canvas.setCursor(4, 30);
      canvas.println("Couldn't reach /tasks.");
      canvas.setCursor(4, 44);
      canvas.println("(endpoint not built yet)");
    } else if (rows.empty()) {
      canvas.setCursor(4, 30);
      canvas.println("Nothing here.");
    } else {
      if (sel < scroll) scroll = sel;
      if (sel >= scroll + 6) scroll = sel - 5;
      for (int i = scroll; i < (int)rows.size() && i < scroll + 6; i++) {
        canvas.setCursor(4, 26 + (i - scroll) * 14);
        canvas.setTextColor(i == sel ? t.accent : t.primary);
        canvas.println(rows[i].title);
      }
    }
    canvas.setCursor(4, 112);
    canvas.setTextColor(t.primary);
    canvas.print(";/.=pick r=refresh ESC=back");
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (!rows.empty()) {
      if (ev.up) sel = (sel - 1 + (int)rows.size()) % (int)rows.size();
      if (ev.down) sel = (sel + 1) % (int)rows.size();
    }
    if (ev.typed.indexOf('r') >= 0) fetch();
    if (ev.escape) return;
    delay(30);
  }
}

void tasksMenuScreen() {
  const char *items[] = {"Active", "Queued", "Finished", "Back"};
  const char *statuses[] = {"active", "queued", "done"};
  int sel = 0;
  while (true) {
    const Theme &t = THEMES[gSettings.themeIndex];
    drawHeader("Tasks");
    for (int i = 0; i < 4; i++) {
      canvas.setCursor(4, 30 + i * 14);
      canvas.setTextColor(i == sel ? t.accent : t.primary);
      canvas.println(items[i]);
    }
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (ev.up) sel = (sel - 1 + 4) % 4;
    if (ev.down) sel = (sel + 1) % 4;
    if (ev.escape) return;
    if (ev.enter) {
      if (sel == 3) return;
      tasksListScreen(items[sel], statuses[sel]);
    }
    delay(30);
  }
}

// ---- Handoff: list + reroute popup with current-agent greyed out ----

struct HandoffRow { String id; String title; String recommended; String currentAgent; String reason; };

static bool fetchHandoffs(std::vector<HandoffRow> &rows) {
  JsonDocument doc;
  if (!httpGetJson("/api/gateway/handoffs", doc)) return false;
  rows.clear();
  for (JsonObject o : doc["handoffs"].as<JsonArray>()) {
    HandoffRow r;
    r.id = o["id"].as<String>();
    r.title = o["title"].as<String>();
    r.recommended = o["recommendedAgent"] | ""; // NOTE: not built server-side yet
    r.currentAgent = o["currentAgent"] | "";     // NOTE: same
    // NOTE: assumed — a short reason shown when there's no recommendation,
    // e.g. "no image gen" if no connected agent covers the task's needed
    // capability. Falls back to a generic message if absent.
    r.reason = o["reason"] | "";
    rows.push_back(r);
  }
  return true;
}

// Shows every connected (enabled) agent. The one currently holding this
// task is greyed out and not selectable — you can only switch to a
// different one. The recommended agent (if any) is marked. Returns the
// chosen agent index, or -1 on ESC/cancel.
static int reroutePopup(const HandoffRow &row) {
  std::vector<int> pickable; // indices into AGENT_NAMES that can be selected
  for (int i = 0; i < AGENT_COUNT; i++) {
    if (gSettings.agentEnabled[i] && !row.currentAgent.equalsIgnoreCase(AGENT_NAMES[i])) {
      pickable.push_back(i);
    }
  }
  if (pickable.empty()) return -1;

  int sel = 0;
  // If there's a recommended agent among the pickable ones, land on it.
  for (size_t i = 0; i < pickable.size(); i++) {
    if (row.recommended.equalsIgnoreCase(AGENT_NAMES[pickable[i]])) { sel = (int)i; break; }
  }

  while (true) {
    const Theme &t = THEMES[gSettings.themeIndex];
    drawHeader(row.title.c_str());
    canvas.setTextSize(1);
    for (int i = 0; i < AGENT_COUNT; i++) {
      if (!gSettings.agentEnabled[i]) continue;
      bool isCurrent = row.currentAgent.equalsIgnoreCase(AGENT_NAMES[i]);
      int pickIdx = -1;
      for (size_t j = 0; j < pickable.size(); j++) if (pickable[j] == i) pickIdx = (int)j;

      int y = 28 + i * 14;
      canvas.setCursor(4, y);
      if (isCurrent) {
        canvas.setTextColor(0x7BEF); // grey — current holder, not selectable
        canvas.print(AGENT_NAMES[i]);
        canvas.print("  (current)");
      } else {
        canvas.setTextColor(pickIdx == sel ? t.accent : t.primary);
        canvas.print(AGENT_NAMES[i]);
        if (row.recommended.equalsIgnoreCase(AGENT_NAMES[i])) canvas.print("  (recommended)");
      }
    }
    canvas.setCursor(4, 112);
    canvas.setTextColor(t.primary);
    canvas.print(";/.=pick ENTER=ok ESC=cancel");
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (ev.up) sel = (sel - 1 + (int)pickable.size()) % (int)pickable.size();
    if (ev.down) sel = (sel + 1) % (int)pickable.size();
    if (ev.escape) return -1;
    if (ev.enter) return pickable[sel];
    delay(30);
  }
}

static bool doReroute(const HandoffRow &row, int agentIdx) {
  JsonDocument resp;
  String body = String("{\"handoffId\":\"") + row.id + "\",\"agentName\":\"" + AGENT_NAMES[agentIdx] + "\"}";
  bool ok = httpPostJson("/api/gateway/handoffs/reroute", body, resp);
  drawHeader(ok ? "Rerouted" : "Reroute failed");
  canvas.pushSprite(0, 0);
  delay(600);
  return ok;
}

void handoffRunScreen(const String &preselectHandoffId) {
  std::vector<HandoffRow> rows;
  bool failed = !fetchHandoffs(rows);

  // Cap-warning alert path: jump straight to the popup for that task.
  if (preselectHandoffId.length() > 0 && !failed) {
    for (auto &r : rows) {
      if (r.id == preselectHandoffId) {
        int agentIdx = reroutePopup(r);
        if (agentIdx >= 0) { doReroute(r, agentIdx); fetchHandoffs(rows); }
        break;
      }
    }
  }

  int sel = 0, scroll = 0;
  while (true) {
    const Theme &t = THEMES[gSettings.themeIndex];
    drawHeader("Handoff");
    if (failed) {
      canvas.setCursor(4, 30);
      canvas.println("Couldn't reach the Gateway.");
    } else if (rows.empty()) {
      canvas.setCursor(4, 30);
      canvas.println("No open handoffs.");
    } else {
      // Card/"island" per task: rounded outline, title + a small badge —
      // either the recommended agent, or a short reason there isn't one
      // (e.g. "no image gen" if no connected agent covers what's needed).
      const int cardH = 34, cardGap = 4, cardX = 4, cardW = 232;
      const int visibleCards = 2;
      if (sel < scroll) scroll = sel;
      if (sel >= scroll + visibleCards) scroll = sel - (visibleCards - 1);

      for (int i = scroll; i < (int)rows.size() && i < scroll + visibleCards; i++) {
        int cardY = 24 + (i - scroll) * (cardH + cardGap);
        bool isSel = (i == sel);
        if (isSel) canvas.fillRoundRect(cardX, cardY, cardW, cardH, 6, t.accent);
        else canvas.drawRoundRect(cardX, cardY, cardW, cardH, 6, t.accent);

        canvas.setTextColor(isSel ? t.bg : t.primary);
        canvas.setCursor(cardX + 8, cardY + 6);
        canvas.println(rows[i].title);

        String badge = rows[i].recommended.length() ? ("recommended: " + rows[i].recommended)
                      : rows[i].reason.length() ? rows[i].reason
                      : "no suggestion yet";
        canvas.setCursor(cardX + 8, cardY + 19);
        canvas.print(badge);
      }
    }
    canvas.setCursor(4, 112);
    canvas.setTextColor(t.primary);
    canvas.print(";/.=pick ENTER=reroute ESC=back");
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (!rows.empty()) {
      if (ev.up) sel = (sel - 1 + (int)rows.size()) % (int)rows.size();
      if (ev.down) sel = (sel + 1) % (int)rows.size();
      if (ev.enter) {
        int agentIdx = reroutePopup(rows[sel]);
        if (agentIdx >= 0) {
          doReroute(rows[sel], agentIdx);
          failed = !fetchHandoffs(rows);
          sel = min(sel, max(0, (int)rows.size() - 1));
        }
      }
    }
    if (ev.typed.indexOf('r') >= 0) failed = !fetchHandoffs(rows);
    if (ev.escape) return;
    delay(30);
  }
}