// wav_writer.cpp
#include "wav_writer.h"

#include <cstdio>
#include <cstring>

namespace ndk_audio {

namespace {
void writeLE32(unsigned char* p, uint32_t v) {
  p[0] = static_cast<unsigned char>(v & 0xFF);
  p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
  p[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
  p[3] = static_cast<unsigned char>((v >> 24) & 0xFF);
}
void writeLE16(unsigned char* p, uint16_t v) {
  p[0] = static_cast<unsigned char>(v & 0xFF);
  p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
}
}  // namespace

bool writeWav(const std::string& path, const int16_t* samples, int32_t numSamples,
              int32_t sampleRate, int16_t channels) {
  if (samples == nullptr || numSamples < 0 || sampleRate <= 0 || channels <= 0) {
    return false;
  }
  const uint32_t bitsPerSample = 16;
  const uint32_t byteRate =
      static_cast<uint32_t>(sampleRate) * static_cast<uint32_t>(channels) * (bitsPerSample / 8);
  const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));
  const uint32_t dataBytes = static_cast<uint32_t>(numSamples) * (bitsPerSample / 8);
  const uint32_t riffSize = 36 + dataBytes;

  unsigned char header[44];
  std::memcpy(header, "RIFF", 4);
  writeLE32(header + 4, riffSize);
  std::memcpy(header + 8, "WAVE", 4);
  std::memcpy(header + 12, "fmt ", 4);
  writeLE32(header + 16, 16);  // PCM fmt chunk size
  writeLE16(header + 20, 1);   // audio format = PCM
  writeLE16(header + 22, static_cast<uint16_t>(channels));
  writeLE32(header + 24, static_cast<uint32_t>(sampleRate));
  writeLE32(header + 28, byteRate);
  writeLE16(header + 32, blockAlign);
  writeLE16(header + 34, static_cast<uint16_t>(bitsPerSample));
  std::memcpy(header + 36, "data", 4);
  writeLE32(header + 40, dataBytes);

  FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  bool ok = (std::fwrite(header, 1, 44, f) == 44);
  if (ok && numSamples > 0) {
    ok = (std::fwrite(samples, sizeof(int16_t), static_cast<size_t>(numSamples), f) ==
          static_cast<size_t>(numSamples));
  }
  std::fclose(f);
  return ok;
}

}  // namespace ndk_audio
