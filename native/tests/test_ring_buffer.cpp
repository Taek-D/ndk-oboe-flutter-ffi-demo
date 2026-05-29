// test_ring_buffer.cpp
//
// 워커 소유 pre-roll 링버퍼: wrap-around 무결성 + overflow(가장 오래된 것 overwrite).
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "ring_buffer.h"

namespace {

using ndk_audio::RingBuffer;

TEST(RingBufferTest, BasicSnapshotInOrder) {
  RingBuffer rb(10);
  std::vector<int16_t> in = {1, 2, 3, 4, 5};
  rb.write(in.data(), 5);
  EXPECT_EQ(rb.size(), 5);
  std::vector<int16_t> out(5, 0);
  ASSERT_EQ(rb.snapshot(out.data(), 5), 5);
  EXPECT_EQ(out, in);  // 시간순 {1,2,3,4,5}
}

// capacity 초과 단일 write → 최근 capacity개만 유지.
TEST(RingBufferTest, OverflowKeepsRecent) {
  RingBuffer rb(5);
  std::vector<int16_t> in = {1, 2, 3, 4, 5, 6, 7};  // 최근 5개 = {3,4,5,6,7}
  rb.write(in.data(), 7);
  EXPECT_EQ(rb.size(), 5);
  std::vector<int16_t> out(5, 0);
  rb.snapshot(out.data(), 5);
  EXPECT_EQ(out, (std::vector<int16_t>{3, 4, 5, 6, 7}));
}

// 증분 write가 wrap-around를 넘어가도 시간순 최근 데이터 유지.
TEST(RingBufferTest, IncrementalWrapAround) {
  RingBuffer rb(5);
  std::vector<int16_t> a = {1, 2, 3};
  std::vector<int16_t> b = {4, 5, 6, 7};  // 누적 7개 → 최근 5개 {3,4,5,6,7}
  rb.write(a.data(), 3);
  rb.write(b.data(), 4);
  std::vector<int16_t> out(5, 0);
  rb.snapshot(out.data(), 5);
  EXPECT_EQ(out, (std::vector<int16_t>{3, 4, 5, 6, 7}));
}

// 요청 n이 size보다 작으면 최근 n개만.
TEST(RingBufferTest, SnapshotFewerThanSize) {
  RingBuffer rb(10);
  std::vector<int16_t> in = {1, 2, 3, 4, 5};
  rb.write(in.data(), 5);
  std::vector<int16_t> out(3, 0);
  ASSERT_EQ(rb.snapshot(out.data(), 3), 3);  // 최근 3개 {3,4,5}
  EXPECT_EQ(out, (std::vector<int16_t>{3, 4, 5}));
}

// 빈 버퍼 스냅샷은 0.
TEST(RingBufferTest, EmptySnapshot) {
  RingBuffer rb(8);
  std::vector<int16_t> out(8, 0);
  EXPECT_EQ(rb.snapshot(out.data(), 8), 0);
}

}  // namespace
