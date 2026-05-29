// test_wav_writer.cpp
//
// 16bit PCM mono WAV: 헤더(RIFF/WAVE/fmt/data) + 실제 sampleRate/길이 검증.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "wav_writer.h"

namespace {

using ndk_audio::writeWav;

uint32_t rdLE32(const unsigned char* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t rdLE16(const unsigned char* p) {
  return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                               (static_cast<uint16_t>(p[1]) << 8));
}

TEST(WavWriterTest, HeaderAndDataSize) {
  const std::string path = "test_out_ndk_audio.wav";
  std::vector<int16_t> samples(48000, 1000);  // 1초 @ 48k mono
  ASSERT_TRUE(writeWav(path, samples.data(), static_cast<int32_t>(samples.size()), 48000, 1));

  FILE* f = std::fopen(path.c_str(), "rb");
  ASSERT_NE(f, nullptr);
  unsigned char h[44];
  const size_t rd = std::fread(h, 1, 44, f);
  std::fclose(f);
  std::remove(path.c_str());
  ASSERT_EQ(rd, 44u);

  EXPECT_EQ(std::string(reinterpret_cast<char*>(h), 4), "RIFF");
  EXPECT_EQ(std::string(reinterpret_cast<char*>(h) + 8, 4), "WAVE");
  EXPECT_EQ(std::string(reinterpret_cast<char*>(h) + 12, 4), "fmt ");
  EXPECT_EQ(rdLE16(h + 20), 1);            // PCM
  EXPECT_EQ(rdLE16(h + 22), 1);            // mono
  EXPECT_EQ(rdLE32(h + 24), 48000u);       // sampleRate
  EXPECT_EQ(rdLE16(h + 34), 16);           // bitsPerSample
  EXPECT_EQ(std::string(reinterpret_cast<char*>(h) + 36, 4), "data");
  EXPECT_EQ(rdLE32(h + 40), 48000u * 2u);  // dataBytes = numSamples * 2
}

TEST(WavWriterTest, RejectsBadArgs) {
  std::vector<int16_t> s(10, 0);
  EXPECT_FALSE(writeWav("x.wav", nullptr, 10, 48000, 1));
  EXPECT_FALSE(writeWav("x.wav", s.data(), 10, 0, 1));
  EXPECT_FALSE(writeWav("x.wav", s.data(), 10, 48000, 0));
}

}  // namespace
