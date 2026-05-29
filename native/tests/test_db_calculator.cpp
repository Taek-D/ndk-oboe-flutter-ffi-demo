// test_db_calculator.cpp
//
// Phase 0-AM 게이트: computeDb의 dBFS 기대값 검증.
// C1 함정 주의 — 기준 입력은 구형/DC(사인파 아님).
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "audio_constants.h"
#include "db_calculator.h"

namespace {

using ndk_audio::computeDb;
using ndk_audio::kFloorDb;
using ndk_audio::kWindowSamples;

// 풀스케일 DC(+32767 상수) -> 0 dBFS (±0.3).
TEST(DbCalculatorTest, FullScaleDcIsZeroDbfs) {
  std::vector<int16_t> buf(kWindowSamples, 32767);
  EXPECT_NEAR(computeDb(buf.data(), static_cast<int32_t>(buf.size())), 0.0f, 0.3f);
}

// 1/10 진폭 DC(+3277 상수) -> -20 dBFS (±0.3).
TEST(DbCalculatorTest, TenthScaleDcIsMinus20Dbfs) {
  std::vector<int16_t> buf(kWindowSamples, 3277);
  EXPECT_NEAR(computeDb(buf.data(), static_cast<int32_t>(buf.size())), -20.0f, 0.3f);
}

// 무음(전부 0) -> -100 (floor 클램프).
TEST(DbCalculatorTest, SilenceIsFloor) {
  std::vector<int16_t> buf(kWindowSamples, 0);
  EXPECT_FLOAT_EQ(computeDb(buf.data(), static_cast<int32_t>(buf.size())), kFloorDb);
}

// 풀스케일 구형파(±32767 교번) -> 0 dBFS. RMS는 DC와 동일.
TEST(DbCalculatorTest, FullScaleSquareIsZeroDbfs) {
  std::vector<int16_t> buf(kWindowSamples);
  for (size_t i = 0; i < buf.size(); ++i) {
    buf[i] = (i % 2 == 0) ? static_cast<int16_t>(32767) : static_cast<int16_t>(-32767);
  }
  EXPECT_NEAR(computeDb(buf.data(), static_cast<int32_t>(buf.size())), 0.0f, 0.3f);
}

// 가드: nullptr/0 count -> floor.
TEST(DbCalculatorTest, NullOrEmptyIsFloor) {
  EXPECT_FLOAT_EQ(computeDb(nullptr, 0), kFloorDb);
  std::vector<int16_t> buf(kWindowSamples, 32767);
  EXPECT_FLOAT_EQ(computeDb(buf.data(), 0), kFloorDb);
}

// (선택, 참고) 풀스케일 사인파 -> -3.01 dBFS. C1 함정 문서화.
TEST(DbCalculatorTest, FullScaleSineIsMinus3Dbfs) {
  constexpr double kPi = 3.14159265358979323846;
  std::vector<int16_t> buf(kWindowSamples);
  for (size_t i = 0; i < buf.size(); ++i) {
    const double phase = 2.0 * kPi * static_cast<double>(i) / 100.0;
    buf[i] = static_cast<int16_t>(std::lround(32767.0 * std::sin(phase)));
  }
  EXPECT_NEAR(computeDb(buf.data(), static_cast<int32_t>(buf.size())), -3.01f, 0.3f);
}

}  // namespace
