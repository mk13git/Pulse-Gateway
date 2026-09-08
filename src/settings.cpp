#include "settings.h"
#include "config.h"
#include <M5GFX.h>

extern M5Canvas canvas; // shared with main.cpp

Settings gSettings;
static Preferences prefs;

// ---- Persistence ----

void settingsLoad() {
  prefs.begin("pulse", /*readOnly=*/false);
  gSettings.wifiSsid = prefs.getString("ssid", "");
  gSettings.wifiPassword = prefs.getString("pass", "");
  gSettings.themeIndex = prefs.getInt("theme", 0);
  gSettings.brightnessPct = prefs.getInt("bright", 80);
  gSettings.volumePct = prefs.getInt("vol", 60);

  gSettings.localAddress = prefs.getString("localip", "");
  gSettings.tailscaleAddress = prefs.getString("tsip", "");
  gSettings.activeConnection = prefs.getInt("activeconn", 0);

  for (int i = 0; i < AGENT_COUNT; i++) {
    String enKey = String("aen") + i;
    String colKey = String("acol") + i;
    gSettings.agentEnabled[i] = prefs.getBool(enKey.c_str(), true);
    gSettings.agentColorIdx[i] = prefs.getInt(colKey.c_str(), gSettings.agentColorIdx[i]);
  }
  gSettings.nextMemoSeq = prefs.getUInt("memoseq", 1);
  prefs.end();

  if (gSettings.themeIndex < 0 || gSettings.themeIndex >= THEME_COUNT) gSettings.themeIndex = 0;
  if (gSettings.brightnessPct < 10) gSettings.brightnessPct = 10;
  if (gSettings.volumePct < 0) gSettings.volumePct = 0;
  if (gSettings.activeConnection != 0 && gSettings.activeConnection != 1) gSettings.activeConnection = 0;
}

void settingsSave() {
  prefs.begin("pulse", false);
  prefs.putString("ssid", gSettings.wifiSsid);
  prefs.putString("pass", gSettings.wifiPassword);
  prefs.putInt("theme", gSettings.themeIndex);
  prefs.putInt("bright", gSettings.brightnessPct);
  prefs.putInt("vol", gSettings.volumePct);

  prefs.putString("localip", gSettings.localAddress);
  prefs.putString("tsip", gSettings.tailscaleAddress);
  prefs.putInt("activeconn", gSettings.activeConnection);

  for (int i = 0; i < AGENT_COUNT; i++) {
    String enKey = String("aen") + i;
    String colKey = String("acol") + i;
    prefs.putBool(enKey.c_str(), gSettings.agentEnabled[i]);
    prefs.putInt(colKey.c_str(), gSettings.agentColorIdx[i]);
  }
  prefs.putUInt("memoseq", gSettings.nextMemoSeq);
  prefs.end();
}

bool settingsHasWifi() {
  return gSettings.wifiSsid.length() > 0;
}

String settingsActiveBaseUrl() {
  String addr = gSettings.activeConnection == 1 ? gSettings.tailscaleAddress : gSettings.localAddress;
  if (addr.length() == 0) return String(PULSE_BASE_URL); // nothing configured yet — fall back
  // NOTE: assumes plain http:// on the LAN/Tailscale address. If you put
  // Pulse behind TLS on your own domain instead, change this to https://.
  return String("http://") + addr;
}

void settingsApplyDisplayAndAudio() {
  M5Cardputer.Display.setBrightness((gSettings.brightnessPct * 255) / 100);
  M5Cardputer.Speaker.setVolume((gSettings.volumePct * 255) / 100);
}

// ---- Shared small helpers ----

KeyEvent pollKeys() {
  KeyEvent ev;
  M5Cardputer.update();
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return ev;

  auto state = M5Cardputer.Keyboard.keysState();
  ev.enter = state.enter;
  ev.back = state.del;
  // NOTE: M5Unified's KeysState doesn't expose a named `.esc` field on all
  // versions — isKeyPressed(KEY_ESC) is the reliable path across versions.
  ev.escape = M5Cardputer.Keyboard.isKeyPressed(KEY_ESC);
  for (char c : state.word) {
    if (c == ',') ev.left = true;
    else if (c == '/') ev.right = true;
    else if (c == ';') ev.up = true;
    else if (c == '.') ev.down = true;
    else ev.typed += c;
  }
  return ev;
}

void drawHeader(const char *title) {
  const Theme &t = THEMES[gSettings.themeIndex];
  canvas.fillScreen(t.bg);
  canvas.setTextColor(t.accent);
  canvas.setTextSize(2);
  canvas.setCursor(4, 4);
  canvas.print(title);
  canvas.setTextColor(t.primary);
  canvas.setTextSize(1);
}

// ---- WiFi scan + setup ----

String textInputScreen(const char *prompt, bool mask) {
  const Theme &t = THEMES[gSettings.themeIndex];
  String value = "";
  while (true) {
    drawHeader(prompt);
    canvas.setCursor(4, 30);
    canvas.setTextSize(1);
    String shown = "";
    for (size_t i = 0; i < value.length(); i++) shown += mask ? '*' : value[i];
    canvas.println(shown + "_");
    canvas.setCursor(4, 110);
    canvas.print("ENTER=ok  DEL=erase  ESC=cancel");
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (ev.enter) return value;
    if (ev.escape) return ""; // ESC always cancels out, regardless of what's typed
    if (ev.back && value.length() > 0) value.remove(value.length() - 1);
    if (ev.typed.length() > 0) value += ev.typed;
    delay(30);
  }
}

void settingsRunWifiSetup() {
  drawHeader("Scanning WiFi...");
  canvas.pushSprite(0, 0);
  int n = WiFi.scanNetworks();

  std::vector<String> names;
  for (int i = 0; i < n && i < 20; i++) names.push_back(WiFi.SSID(i));
  names.push_back("[ Enter manually ]");

  int sel = 0;
  while (true) {
    drawHeader("Pick WiFi");
    for (size_t i = 0; i < names.size() && i < 8; i++) {
      canvas.setCursor(4, 30 + i * 12);
      if ((int)i == sel) canvas.setTextColor(THEMES[gSettings.themeIndex].accent);
      else canvas.setTextColor(THEMES[gSettings.themeIndex].primary);
      canvas.println(names[i]);
    }
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (ev.up && sel > 0) sel--;
    if (ev.down && sel < (int)names.size() - 1) sel++;
    if (ev.escape) return; // back out of WiFi setup entirely
    if (ev.enter) break;
    delay(30);
  }

  String ssid = (names[sel] == "[ Enter manually ]") ? textInputScreen("SSID:", false) : names[sel];
  if (ssid.length() == 0) return;
  String pass = textInputScreen("Password:", true);

  gSettings.wifiSsid = ssid;
  gSettings.wifiPassword = pass;
  settingsSave();

  drawHeader("Saved");
  canvas.setCursor(4, 30);
  canvas.println(ssid);
  canvas.println("Reconnecting...");
  canvas.pushSprite(0, 0);
  delay(800);
}

// ---- Theme / brightness / volume pickers ----

void themePickerScreen() {
  while (true) {
    const Theme &t = THEMES[gSettings.themeIndex];
    drawHeader("Theme");
    canvas.setCursor(4, 40);
    canvas.setTextSize(2);
    canvas.setTextColor(t.accent);
    canvas.println(t.name);
    canvas.setTextSize(1);
    canvas.setTextColor(t.primary);
    canvas.setCursor(4, 70);
    canvas.printf("%d / %d", gSettings.themeIndex + 1, THEME_COUNT);
    canvas.setCursor(4, 110);
    canvas.print(",/= change  ENTER/ESC=ok");
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (ev.left) gSettings.themeIndex = (gSettings.themeIndex - 1 + THEME_COUNT) % THEME_COUNT;
    if (ev.right) gSettings.themeIndex = (gSettings.themeIndex + 1) % THEME_COUNT;
    if (ev.enter || ev.escape) { settingsSave(); return; }
    delay(30);
  }
}

// Shared bar-style picker for brightness/volume, both 10% steps.
int percentPickerScreen(const char *title, int startPct, int minPct) {
  int pct = startPct;
  while (true) {
    const Theme &t = THEMES[gSettings.themeIndex];
    drawHeader(title);
    canvas.setTextSize(2);
    canvas.setCursor(4, 40);
    canvas.setTextColor(t.accent);
    canvas.printf("%d%%", pct);
    canvas.setTextSize(1);
    canvas.setTextColor(t.primary);

    int barW = 220, barX = 4, barY = 70;
    canvas.drawRect(barX, barY, barW, 10, t.primary);
    canvas.fillRect(barX, barY, (barW * pct) / 100, 10, t.accent);

    canvas.setCursor(4, 110);
    canvas.print(",/= adjust  ENTER/ESC=ok");
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (ev.left) pct = max(minPct, pct - 10);
    if (ev.right) pct = min(100, pct + 10);
    if (ev.enter || ev.escape) return pct;
    delay(30);
  }
}

// ---- Agents: Colors ----

void agentColorsScreen() {
  int sel = 0;
  while (true) {
    const Theme &t = THEMES[gSettings.themeIndex];
    drawHeader("Agent Colors");
    for (int i = 0; i < AGENT_COUNT; i++) {
      if (!gSettings.agentEnabled[i]) continue; // disconnected agents don't appear
      canvas.setCursor(4, 30 + i * 14);
      canvas.setTextColor(i == sel ? t.accent : t.primary);
      canvas.print(AGENT_NAMES[i]);
      canvas.print("  ");
      int dotX = canvas.getCursorX() + 4;
      int dotY = 34 + i * 14;
      canvas.fillCircle(dotX, dotY, 4, AGENT_COLOR_SWATCHES[gSettings.agentColorIdx[i]].color);
      canvas.setCursor(dotX + 12, 30 + i * 14);
      canvas.print(AGENT_COLOR_SWATCHES[gSettings.agentColorIdx[i]].name);
    }
    canvas.setCursor(4, 110);
    canvas.print(";/.=pick agent ,/== color  ESC=back");
    canvas.pushSprite(0, 0);

    // Move selection only across enabled agents.
    KeyEvent ev = pollKeys();
    if (ev.up) { do { sel = (sel - 1 + AGENT_COUNT) % AGENT_COUNT; } while (!gSettings.agentEnabled[sel]); }
    if (ev.down) { do { sel = (sel + 1) % AGENT_COUNT; } while (!gSettings.agentEnabled[sel]); }
    if (ev.left) gSettings.agentColorIdx[sel] = (gSettings.agentColorIdx[sel] - 1 + AGENT_SWATCH_COUNT) % AGENT_SWATCH_COUNT;
    if (ev.right) gSettings.agentColorIdx[sel] = (gSettings.agentColorIdx[sel] + 1) % AGENT_SWATCH_COUNT;
    if (ev.escape) { settingsSave(); return; }
    delay(30);
  }
}

// ---- Agents: Setup ----
// Per your setup (server already logged into each CLI via subscription),
// this is intentionally just an on/off toggle per agent — no credential
// fields on-device. See localAddress/tailscaleAddress below for how the
// device finds that server at all.

void agentSetupScreen() {
  int sel = 0;
  while (true) {
    const Theme &t = THEMES[gSettings.themeIndex];
    drawHeader("Agent Setup");
    for (int i = 0; i < AGENT_COUNT; i++) {
      canvas.setCursor(4, 30 + i * 14);
      canvas.setTextColor(i == sel ? t.accent : t.primary);
      canvas.print(AGENT_NAMES[i]);
      canvas.print(": ");
      canvas.print(gSettings.agentEnabled[i] ? "ON" : "OFF");
    }
    canvas.setCursor(4, 110);
    canvas.print(";/.=select ENTER=toggle ESC=back");
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (ev.up) sel = (sel - 1 + AGENT_COUNT) % AGENT_COUNT;
    if (ev.down) sel = (sel + 1) % AGENT_COUNT;
    if (ev.enter) gSettings.agentEnabled[sel] = !gSettings.agentEnabled[sel];
    if (ev.escape) { settingsSave(); return; }
    delay(30);
  }
}

void agentsMenuScreen() {
  const char *items[] = {"Colors", "Setup", "Back"};
  int sel = 0;
  while (true) {
    drawHeader("Agents");
    for (int i = 0; i < 3; i++) {
      canvas.setCursor(4, 30 + i * 14);
      canvas.setTextColor(i == sel ? THEMES[gSettings.themeIndex].accent : THEMES[gSettings.themeIndex].primary);
      canvas.println(items[i]);
    }
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (ev.up) sel = (sel - 1 + 3) % 3;
    if (ev.down) sel = (sel + 1) % 3;
    if (ev.escape) return;
    if (ev.enter) {
      switch (sel) {
        case 0: agentColorsScreen(); break;
        case 1: agentSetupScreen(); break;
        case 2: return;
      }
    }
    delay(30);
  }
}

// ---- Server connection: local IP / Tailscale IP + quick switch ----

void connectionScreen() {
  int sel = 0; // 0 = local field, 1 = tailscale field, 2 = active toggle
  while (true) {
    const Theme &t = THEMES[gSettings.themeIndex];
    drawHeader("Connection");

    canvas.setCursor(4, 30);
    canvas.setTextColor(sel == 0 ? t.accent : t.primary);
    canvas.print("Local:  ");
    canvas.fillCircle(canvas.getCursorX() + 4, 34, 4, 0x07E0); // green
    canvas.setCursor(canvas.getCursorX() + 14, 30);
    canvas.println(gSettings.localAddress.length() ? gSettings.localAddress : "(not set)");

    canvas.setCursor(4, 46);
    canvas.setTextColor(sel == 1 ? t.accent : t.primary);
    canvas.print("Tailsc: ");
    canvas.fillCircle(canvas.getCursorX() + 4, 50, 4, 0x001F); // blue
    canvas.setCursor(canvas.getCursorX() + 14, 46);
    canvas.println(gSettings.tailscaleAddress.length() ? gSettings.tailscaleAddress : "(not set)");

    canvas.setCursor(4, 66);
    canvas.setTextColor(sel == 2 ? t.accent : t.primary);
    canvas.print("Active: ");
    canvas.println(gSettings.activeConnection == 1 ? "Tailscale (1)" : "Local (0)");

    canvas.setTextColor(t.primary);
    canvas.setCursor(4, 110);
    canvas.print(";/.=pick ENTER=edit/toggle 0/1=quick ESC=back");
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (ev.up) sel = (sel - 1 + 3) % 3;
    if (ev.down) sel = (sel + 1) % 3;
    if (ev.typed.indexOf('0') >= 0) gSettings.activeConnection = 0;
    if (ev.typed.indexOf('1') >= 0) gSettings.activeConnection = 1;
    if (ev.enter) {
      if (sel == 0) gSettings.localAddress = textInputScreen("Local IP:port", false);
      else if (sel == 1) gSettings.tailscaleAddress = textInputScreen("Tailscale IP:port", false);
      else gSettings.activeConnection = gSettings.activeConnection == 0 ? 1 : 0;
    }
    if (ev.escape) { settingsSave(); return; }
    delay(30);
  }
}

// ---- Top-level menu ----

void settingsRunMenu() {
  const char *items[] = {"WiFi", "Theme", "Brightness", "Volume", "Agents", "Connection", "Back"};
  const int count = 7;
  int sel = 0;
  while (true) {
    drawHeader("Settings");
    for (int i = 0; i < count; i++) {
      canvas.setCursor(4, 30 + i * 12);
      canvas.setTextColor(i == sel ? THEMES[gSettings.themeIndex].accent : THEMES[gSettings.themeIndex].primary);
      canvas.println(items[i]);
    }
    canvas.pushSprite(0, 0);

    KeyEvent ev = pollKeys();
    if (ev.up) sel = (sel - 1 + count) % count;
    if (ev.down) sel = (sel + 1) % count;
    if (ev.escape) return; // back out of settings entirely, to the tab bar

    if (ev.enter) {
      switch (sel) {
        case 0: settingsRunWifiSetup(); break;
        case 1: themePickerScreen(); break;
        case 2:
          gSettings.brightnessPct = percentPickerScreen("Brightness", gSettings.brightnessPct, 10);
          settingsSave();
          settingsApplyDisplayAndAudio();
          break;
        case 3:
          gSettings.volumePct = percentPickerScreen("Volume", gSettings.volumePct, 0);
          settingsSave();
          settingsApplyDisplayAndAudio();
          break;
        case 4: agentsMenuScreen(); break;
        case 5: connectionScreen(); break;
        case 6: return;
      }
    }
    delay(30);
  }
}