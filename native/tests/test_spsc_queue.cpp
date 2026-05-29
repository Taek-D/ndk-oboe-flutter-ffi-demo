// test_spsc_queue.cpp
//
// lock-free SPSC 큐: 순서 보존, full drop, 그리고 단일 생산자/소비자 2스레드
// 무손실 + 순서(ThreadSanitizer로 race 0 검증).
#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "spsc_queue.h"

namespace {

using ndk_audio::SpscQueue;

TEST(SpscQueueTest, PushPopOrder) {
  SpscQueue<int> q(8);
  EXPECT_TRUE(q.push(1));
  EXPECT_TRUE(q.push(2));
  EXPECT_TRUE(q.push(3));
  int v = 0;
  ASSERT_TRUE(q.pop(v));
  EXPECT_EQ(v, 1);
  ASSERT_TRUE(q.pop(v));
  EXPECT_EQ(v, 2);
  ASSERT_TRUE(q.pop(v));
  EXPECT_EQ(v, 3);
  EXPECT_FALSE(q.pop(v));  // empty
}

TEST(SpscQueueTest, FullDrops) {
  SpscQueue<int> q(3);  // capacity() == 2
  EXPECT_EQ(q.capacity(), 2u);
  EXPECT_TRUE(q.push(1));
  EXPECT_TRUE(q.push(2));
  EXPECT_FALSE(q.push(3));  // full → drop
  int v = 0;
  ASSERT_TRUE(q.pop(v));
  EXPECT_EQ(v, 1);
  EXPECT_TRUE(q.push(3));  // 한 칸 비었으니 성공
}

// 단일 생산자/단일 소비자 2스레드. 전량 무손실 + 순서 보존.
// 생산자는 full이면 yield 후 재시도, 소비자는 N개 받을 때까지 pop.
// ThreadSanitizer 빌드에서 race 0이어야 한다.
TEST(SpscQueueTest, ThreadedNoLossInOrder) {
  SpscQueue<int> q(1024);
  const int N = 100000;

  std::thread producer([&] {
    for (int i = 0; i < N; ++i) {
      while (!q.push(i)) {
        std::this_thread::yield();
      }
    }
  });

  std::vector<int> got;
  got.reserve(static_cast<size_t>(N));
  int v = 0;
  while (static_cast<int>(got.size()) < N) {
    if (q.pop(v)) {
      got.push_back(v);
    } else {
      std::this_thread::yield();
    }
  }
  producer.join();

  ASSERT_EQ(static_cast<int>(got.size()), N);
  for (int i = 0; i < N; ++i) {
    ASSERT_EQ(got[static_cast<size_t>(i)], i);  // FIFO 순서 보존
  }
}

}  // namespace
