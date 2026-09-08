#include "net.h"
#include "config.h"
#include "settings.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

bool httpGetJson(const String &path, JsonDocument &doc) {
  WiFiClientSecure client;
  client.setInsecure(); // NOTE: fine for a home project; pin the cert for anything exposed publicly
  HTTPClient http;
  String url = settingsActiveBaseUrl() + path;
  if (!http.begin(client, url)) return false;
  http.addHeader("Authorization", String("Bearer ") + STATION_TOKEN);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    DeserializationError err = deserializeJson(doc, http.getStream());
    ok = !err;
  }
  http.end();
  return ok;
}

bool httpPostJson(const String &path, const String &jsonBody, JsonDocument &responseDoc) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = settingsActiveBaseUrl() + path;
  if (!http.begin(client, url)) return false;
  http.addHeader("Authorization", String("Bearer ") + STATION_TOKEN);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(jsonBody);
  bool ok = false;
  if (code == 200) {
    DeserializationError err = deserializeJson(responseDoc, http.getStream());
    ok = !err;
  }
  http.end();
  return ok;
}

bool uploadWavBuffer(const uint8_t *wavBytes, size_t wavLen, JsonDocument &responseDoc) {
  const char *boundary = "----PulseCardputerBoundary";
  String head = String("--") + boundary + "\r\n" +
                "Content-Disposition: form-data; name=\"audio\"; filename=\"voice.wav\"\r\n" +
                "Content-Type: audio/wav\r\n\r\n";
  String tail = String("\r\n--") + boundary + "--\r\n";

  uint32_t contentLength = head.length() + wavLen + tail.length();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = settingsActiveBaseUrl() + "/api/gateway/voice";
  if (!http.begin(client, url)) return false;
  http.addHeader("Authorization", String("Bearer ") + STATION_TOKEN);
  http.addHeader("Content-Type", String("multipart/form-data; boundary=") + boundary);

  // Build head + body + tail into one buffer, same approach as before —
  // comfortably fits PSRAM at MAX_RECORD_SECONDS lengths.
  uint8_t *full = (uint8_t *)ps_malloc(contentLength);
  if (!full) {
    http.end();
    return false;
  }
  memcpy(full, head.c_str(), head.length());
  memcpy(full + head.length(), wavBytes, wavLen);
  memcpy(full + head.length() + wavLen, tail.c_str(), tail.length());

  int code = http.POST(full, contentLength);
  free(full);

  bool ok = false;
  if (code == 200) {
    DeserializationError err = deserializeJson(responseDoc, http.getStream());
    ok = !err;
  }
  http.end();
  return ok;
}