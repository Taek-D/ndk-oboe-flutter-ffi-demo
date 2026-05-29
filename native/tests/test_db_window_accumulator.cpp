// test_db_window_accumulator.cpp
//
// C2: 가변 numFrames 누산 → kWindowSamples(6000)마다 dB 1회 방출 + carry-over.
// C3: 가변청크(7·192·4096·1·5800·2904) 입력이 통째 입력과 방출 시퀀스·carry 동일.
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "audio_constants.h"
#include "db_window_accumulator.h"

namespace {

using ndk_audio::DbWindowAccumulator;
using ndk_audio::kFloorDb;
using ndk_audio::kWindowSamples;

struct Sink {
  std::vector<float> dbs;
  static void emit(float db, void* user) {
    static_cast<Sink*>(user)->dbs.push_back(db);
  }
};

// 6000 DC(+3277) 단일 push → 1회 방출 ≈ -20 dBFS, carry 0.
TEST(DbWindowAccumulatorTest, SingleWindowDc) {
  DbWindowAccumulator acc;
  Sink s;
  acc.setEmitCallback(&Sink::emit, &s);
  std::vector<int16_t> buf(kWindowSamples, 3277);
  acc.push(buf.data(), kWindowSamples);
  ASSERT_EQ(s.dbs.size(), 1u);
  EXPECT_NEAR(s.dbs[0], -20.0f, 0.3f);
  EXPECT_EQ(acc.pendingSampleCount(), 0);
}

// 무음 6000 → floor(-100).
TEST(DbWindowAccumulatorTest, SilenceWindow) {
  DbWindowAccumulator acc;
  Sink s;
  acc.setEmitCallback(&Sink::emit, &s);
  std::vector<int16_t> buf(kWindowSamples, 0);
  acc.push(buf.data(), kWindowSamples);
  ASSERT_EQ(s.dbs.size(), 1u);
  EXPECT_FLOAT_EQ(s.dbs[0], kFloorDb);
}

// C3: 가변청크 vs 통째 등가(방출 횟수 / 각 dB / carry 동일).
TEST(DbWindowAccumulatorTest, VariableChunkEquivalence) {
  const int32_t total = 13000;  // 6000*2 + 1000.
  std::vector<int16_t> data(static_cast<size_t>(total));
  for (int32_t i = 0; i < total; ++i) {
    data[static_cast<size_t>(i)] = static_cast<int16_t>(((i * 37) % 1000) - 500);
  }

  // (a) 가변청크.
  DbWindowAccumulator accA;
  Sink sA;
  accA.setEmitCallback(&Sink::emit, &sA);
  const int32_t chunks[] = {7, 192, 4096, 1, 5800, 2904};
  int32_t off = 0;
  for (int32_t c : chunks) {
    accA.push(data.data() + off, c);
    off += c;
  }
  ASSERT_EQ(off, total);

  // (b) 통째.
  DbWindowAccumulator accB;
  Sink sB;
  accB.setEmitCallback(&Sink::emit, &sB);
  accB.push(data.data(), total);

  ASSERT_EQ(sA.dbs.size(), 2u);
  ASSERT_EQ(sA.dbs.size(), sB.dbs.size());
  for (size_t i = 0; i < sA.dbs.size(); ++i) {
    EXPECT_NEAR(sA.dbs[i], sB.dbs[i], 1e-4f);
  }
  EXPECT_EQ(accA.pendingSampleCount(), 1000);
  EXPECT_EQ(accB.pendingSampleCount(), 1000);
  EXPECT_EQ(accA.pendingSampleCount(), accB.pendingSampleCount());
}

// 한 번의 큰 push가 여러 윈도우를 채우면 윈도우마다 반복 방출.
TEST(DbWindowAccumulatorTest, MultiWindowSinglePush) {
  DbWindowAccumulator acc;
  Sink s;
  acc.setEmitCallback(&Sink::emit, &s);
  std::vector<int16_t> buf(static_cast<size_t>(kWindowSamples * 3), 3277);
  acc.push(buf.data(), kWindowSamples * 3);
  EXPECT_EQ(s.dbs.size(), 3u);
  EXPECT_EQ(acc.pendingSampleCount(), 0);
}

// reset 후 carry-over가 비워진다.
TEST(DbWindowAccumulatorTest, ResetClearsCarry) {
  DbWindowAccumulator acc;
  Sink s;
  acc.setEmitCallback(&Sink::emit, &s);
  std::vector<int16_t> buf(1000, 3277);
  acc.push(buf.data(), 1000);
  EXPECT_EQ(acc.pendingSampleCount(), 1000);
  acc.reset();
  EXPECT_EQ(acc.pendingSampleCount(), 0);
}

}  // namespace
