#pragma once
#include <Arduino.h>

// Writes a 44-byte canonical WAV header for 16-bit mono PCM directly into
// `out` (caller-allocated, >= 44 bytes). `dataBytes` is the size of the PCM
// payload that follows the header.
inline void writeWavHeader(uint8_t *out, uint32_t dataBytes, uint32_t sampleRate) {
  uint32_t byteRate = sampleRate * 2; // mono, 16-bit
  uint32_t chunkSize = 36 + dataBytes;

  memcpy(out + 0, "RIFF", 4);
  memcpy(out + 4, &chunkSize, 4);
  memcpy(out + 8, "WAVE", 4);
  memcpy(out + 12, "fmt ", 4);
  uint32_t subchunk1Size = 16;
  memcpy(out + 16, &subchunk1Size, 4);
  uint16_t audioFormat = 1; // PCM
  memcpy(out + 20, &audioFormat, 2);
  uint16_t numChannels = 1;
  memcpy(out + 22, &numChannels, 2);
  memcpy(out + 24, &sampleRate, 4);
  memcpy(out + 28, &byteRate, 4);
  uint16_t blockAlign = 2;
  memcpy(out + 32, &blockAlign, 2);
  uint16_t bitsPerSample = 16;
  memcpy(out + 34, &bitsPerSample, 2);
  memcpy(out + 36, "data", 4);
  memcpy(out + 40, &dataBytes, 4);
}

#define WAV_HEADER_SIZE 44