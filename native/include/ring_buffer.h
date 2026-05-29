// ring_buffer.h
//
// 워커 스레드가 단독 소유하는 pre-roll 순환 버퍼(C4-R).
// int16 PCM을 wrap-around로 기록하고 가장 오래된 샘플을 overwrite한다.
// 단일 스레드(저장 워커) 소유이므로 락이 필요 없다. 콜백은 이 버퍼를 직접
// 만지지 않는다(콜백 → SPSC 큐 → 워커가 여기에 기록). 트리거 시 워커가
// 최근 kPrerollSamples를 시간순으로 스냅샷한다.
#ifndef NDK_AUDIO_RING_BUFFER_H_
#define NDK_AUDIO_RING_BUFFER_H_

#include <cstdint>
#include <vector>

namespace ndk_audio {

class RingBuffer {
 public:
  explicit RingBuffer(int32_t capacity)
      : buf_(static_cast<size_t>(capacity > 0 ? capacity : 1), 0),
        capacity_(capacity > 0 ? capacity : 1),
        head_(0),
        filled_(0) {}

  int32_t capacity() const { return capacity_; }
  int32_t size() const { return filled_; }  // 현재 보관 중인 유효 샘플 수(<= capacity).

  // count개 샘플을 기록한다. 총량이 capacity를 넘으면 가장 오래된 것을 overwrite.
  void write(const int16_t* data, int32_t count) {
    if (data == nullptr || count <= 0) return;
    if (count >= capacity_) {
      // 한 번에 capacity 이상이 들어오면 마지막 capacity개만 의미 있다.
      const int16_t* src = data + (count - capacity_);
      for (int32_t i = 0; i < capacity_; ++i) buf_[static_cast<size_t>(i)] = src[i];
      head_ = 0;
      filled_ = capacity_;
      return;
    }
    for (int32_t i = 0; i < count; ++i) {
      buf_[static_cast<size_t>(head_)] = data[i];
      head_ = (head_ + 1) % capacity_;
    }
    filled_ = (filled_ + count < capacity_) ? (filled_ + count) : capacity_;
  }

  // 최근 n개(<= size())를 시간순(가장 오래된 것부터)으로 out에 복사. 실제 복사 수 반환.
  int32_t snapshot(int16_t* out, int32_t n) const {
    if (out == nullptr || n <= 0 || filled_ == 0) return 0;
    const int32_t want = (n < filled_) ? n : filled_;
    int32_t start = (head_ - want) % capacity_;
    if (start < 0) start += capacity_;
    for (int32_t i = 0; i < want; ++i) {
      out[static_cast<size_t>(i)] = buf_[static_cast<size_t>((start + i) % capacity_)];
    }
    return want;
  }

  void clear() { head_ = 0; filled_ = 0; }

 private:
  std::vector<int16_t> buf_;
  int32_t capacity_;
  int32_t head_;    // 다음 쓸 위치.
  int32_t filled_;  // 유효 샘플 수.
};

}  // namespace ndk_audio

#endif  // NDK_AUDIO_RING_BUFFER_H_
