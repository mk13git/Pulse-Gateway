#pragma once

// --- Fill these in before flashing ---
// WiFi is no longer set here — on first boot with nothing saved, the device
// walks you through picking a network and typing a password on-device, and
// remembers it across reboots. Press 'S' any time to change it later.

// Your deployed Pulse origin, no trailing slash, e.g. https://pulse-xyz.vercel.app
#define PULSE_BASE_URL "https://your-pulse-deploy.vercel.app"

// Station bearer token — issued from the "Stations" page in Pulse
// (Pair a station -> kind "Cardputer" -> copy the pulse_... token shown once)
#define STATION_TOKEN "pulse_xxxxxxxxxxxxxxxxxxxxxxxxxxxx"

// --- Behavior tuning ---

// How often to poll for new signals/handoffs (ms). Keep this above ~3000 to
// stay comfortably under any rate limit and to save battery.
#define POLL_INTERVAL_MS 5000

// Max recording length for a voice task (seconds). Whisper handles longer
// clips fine; this is mostly a battery/RAM guard for the device.
#define MAX_RECORD_SECONDS 20

// I2S mic sample rate. 16kHz mono is what Whisper expects and is plenty for
// speech - don't raise this, it just burns RAM/upload time.
#define MIC_SAMPLE_RATE 16000

// A Control-key press shorter than this is a "tap" (candidate for a
// double-tap toggle), longer is a genuine hold-to-record. Tune after
// holding the real device — this is a guess.
#define TAP_THRESHOLD_MS 250

// Two taps closer together than this count as a double-tap (toggles
// hands-free recording on). Tune after real-hardware testing.
#define DOUBLE_TAP_WINDOW_MS 400