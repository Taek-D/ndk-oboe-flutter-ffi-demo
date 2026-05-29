// spsc_queue.h
//
// 단일 생산자(오디오 콜백) / 단일 소비자(저장 워커) lock-free 큐.
// std::atomic acquire/release 메모리 순서. 고정 용량. full이면 push 실패(drop),
// empty면 pop 실패. 콜백 경로에서 락/할당 없이 안전하게 워커로 데이터를 넘긴다.
#ifndef NDK_AUDIO_SPSC_QUEUE_H_
#define NDK_AUDIO_SPSC_QUEUE_H_

#include <atomic>
#include <cstddef>
#include <vector>

namespace ndk_audio {

template <typename T>
class SpscQueue {
 public:
  // 실제 보관 가능 개수 = capacity - 1 (한 칸은 full/empty 구분용 sentinel).
  explicit SpscQueue(size_t capacity)
      : buf_(capacity < 2 ? 2 : capacity),
        cap_(capacity < 2 ? 2 : capacity),
        head_(0),
        tail_(0) {}

  // 생산자 전용. 성공 true, full이면 false(drop).
  bool push(const T& item) {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t next = (tail + 1) % cap_;
    if (next == head_.load(std::memory_order_acquire)) {
      return false;  // full
    }
    buf_[tail] = item;
    tail_.store(next, std::memory_order_release);
    return true;
  }

  // 소비자 전용. 성공 true, empty면 false.
  bool pop(T& out) {
    const size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) {
      return false;  // empty
    }
    out = buf_[head];
    head_.store((head + 1) % cap_, std::memory_order_release);
    return true;
  }

  // 보관 가능한 최대 원소 수.
  size_t capacity() const { return cap_ - 1; }

 private:
  std::vector<T> buf_;
  size_t cap_;
  std::atomic<size_t> head_;  // 소비자가 다음 읽을 위치.
  std::atomic<size_t> tail_;  // 생산자가 다음 쓸 위치.
};

}  // namespace ndk_audio

#endif  // NDK_AUDIO_SPSC_QUEUE_H_
