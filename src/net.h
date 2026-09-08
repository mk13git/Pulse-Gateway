#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// GET <settingsActiveBaseUrl()><path>, bearer-authed with STATION_TOKEN.
bool httpGetJson(const String &path, JsonDocument &doc);

// POST <settingsActiveBaseUrl()><path> with a small JSON body, bearer-authed.
// Used for actions like handoff reroute rather than file uploads.
bool httpPostJson(const String &path, const String &jsonBody, JsonDocument &responseDoc);

// POSTs a complete, already-headered WAV buffer to /api/gateway/voice as
// multipart/form-data. Used both for a fresh live recording and for
// resending a recording previously saved to SD.
bool uploadWavBuffer(const uint8_t *wavBytes, size_t wavLen, JsonDocument &responseDoc);