# Pulse Gateway firmware — M5Stack Cardputer ADV

Home screen: idle-animated retro sine waveform (goes live while you're
recording), a recording timer, battery % with a connection-color dot
(green = Local, blue = Tailscale), and small colored per-agent activity
dots (solid = queued, slow blink = working, gone entirely when idle).

Tabs along the bottom: **Tasks, Rec., Handoff, Usage, Settings**. ESC
always steps back one level — it never jumps straight to the home
screen.

## Control key

- **Hold** → records while held, sends the moment you release. Short and
  disposable — if there's no connection when you release, it just fails
  with a message rather than being saved.
- **Two quick taps** → toggles hands-free recording on. Press once more to
  stop, and you'll get a **Save to SD / Send now** prompt (ESC defaults to
  Save, so you never lose a take by accident).

## Settings

WiFi, Theme, Brightness, Volume, then:

- **Agents** → **Colors** (pick each connected agent's dot color) /
  **Setup** (on/off per agent — no credentials live on this device, your
  server holds those)
- **Connection** → Local IP and Tailscale IP fields, plus a quick switch:
  key `0` = Local, key `1` = Tailscale

## Recordings

List of saved memos (`MEMO-00001`, `MEMO-00002`, ...), green dot if
already sent, yellow if not. ENTER opens Play / Pause / Send / Rename /
Delete. Renaming fully replaces the MEMO number — it's gone once renamed.

## Tasks

Tasks -> **Active / Queued / Finished** submenu, each a filtered list.
Needs `GET /api/gateway/tasks` on the Pulse app — doesn't exist yet, so
these fail gracefully to a message until it's added.

## Handoff

List of open handoffs, shown as rounded card "islands" (title + a badge
line: either `recommended: <Agent>`, or a short reason like "no image
gen" when there isn't a recommendation). ENTER opens a reroute popup showing every
connected agent — whoever currently holds that task is greyed out and
not selectable, and the server's recommended agent (if any) is marked
"(recommended)". Confirms via `POST /api/gateway/handoffs/reroute`.

**Cap-warning alert:** when an agent nears its usage cap (90% is a
sensible default — see `usageColor()` in tabs.cpp), the server should
fire a new `cap_warning` signal with a `handoffId`. The device plays a
distinct triple-beep, jumps straight to Handoff, and opens the reroute
popup for that task already — one ENTER on your pick and it's done.
None of this (the signal kind, the 90% threshold, `recommendedAgent`,
`currentAgent`) is wired up server-side yet.

## Usage

Per-agent bar showing how close each is to its cap — reuses
`GET /api/gateway/agents`, tries a direct `usagePct` field first and
falls back to computing one from `used`/`cap` if that's what's there
instead.

## Building

Same GitHub Actions cloud-build flow as before — push these files, the
workflow compiles and merges a flashable `.bin`. `platformio.ini` uses
unpinned M5Unified/M5Cardputer versions (the fix that got run #8/#9
green) — don't re-pin them.

## Known unknowns — expect a round of real-hardware fixes

- `KEY_LEFT_CTRL` constant name, and `Speaker.playRaw()`'s exact argument
  order, are both written against documented APIs but unverified against
  your installed library version.
- Hold-vs-tap timing (`TAP_THRESHOLD_MS` / `DOUBLE_TAP_WINDOW_MS` in
  config.h) are guesses — tune once you can feel the real key travel.
- SD card mount uses a plain `SD.begin()`, relying on M5Unified's
  auto-detected pins for the ADV. If it doesn't mount, that's the first
  thing to check.