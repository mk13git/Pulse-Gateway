#pragma once
#include <Arduino.h>
#include <vector>

// A saved voice memo. Audio lives on SD as /recordings/<seq>.wav; this
// struct is the in-memory mirror of /recordings/index.json.
struct RecordingEntry {
  uint32_t seq;     // stable id, also the filename — never reused
  String label;     // "MEMO-00001" until renamed, then fully replaced
  bool sent;         // green dot vs yellow dot in the list
};

// Mounts the SD card, creates /recordings if missing, loads the index.
// Call once from setup(). If SD mount fails, recordings are silently
// unavailable (isReady() below) rather than crashing the rest of the device.
void recordingsInit();
bool recordingsReady();

std::vector<RecordingEntry> &recordingsList();

// Saves a fresh capture (raw PCM16 mono samples) as a new MEMO-#####,
// unsent. Returns the new entry's index in recordingsList(), or -1 on
// failure (e.g. SD not present).
int recordingsCreate(const int16_t *samples, size_t sampleCount);

// Reads the saved WAV back off SD and uploads it via the same endpoint as a
// live recording. Marks the entry sent (green dot) on success.
bool recordingsSend(int idx);

void recordingsRename(int idx, const String &newLabel); // full replace, no MEMO number kept
void recordingsDelete(int idx);

// Starts/stops playback of a saved recording through the speaker.
// NOTE: M5Unified's speaker API doesn't reliably support true sample-accurate
// pause/resume across versions — "Pause" here stops playback; selecting
// Play again restarts the clip from the beginning. Flag for hardware tuning.
void recordingsPlay(int idx);
void recordingsPause();

// Blocking screen: list of memos with green/yellow status dots; ENTER opens
// a Play/Pause/Send/Rename/Delete popup for the selected one. ESC backs out
// one level at a time (popup -> list -> caller), never jumps to home.
void recordingsRunScreen();