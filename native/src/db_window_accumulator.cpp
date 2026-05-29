// db_window_accumulator.cpp
#include "db_window_accumulator.h"

#include <cmath>

#include "audio_constants.h"

namespace ndk_audio {

DbWindowAccumulator::DbWindowAccumulator()
    : sumSq_(0.0), count_(0), emitFn_(nullptr), emitUser_(nullptr) {}

void DbWindowAccumulator::setEmitCallback(DbEmitFn fn, void* user) {
  emitFn_ = fn;
  emitUser_ = user;
}

void DbWindowAccumulator::reset() {
  sumSq_ = 0.0;
  count_ = 0;
}

void DbWindowAccumulator::push(const int16_t* frames, int32_t numFrames) {
  if (frames == nullptr || numFrames <= 0) {
    return;
  }
  // O(numFrames) 1패스, 동적 할당 없음. sumSq 러닝합만 유지(샘플 버퍼 carry-over X).
  for (int32_t i = 0; i < numFrames; ++i) {
    const double s = static_cast<double>(frames[i]);
    sumSq_ += s * s;
    ++count_;
    if (count_ >= kWindowSamples) {
      // 윈도우(6000샘플) 완성 → dBFS 1회 방출 후 부분합 리셋.
      const double meanSq = sumSq_ / static_cast<double>(count_);
      const double rms = std::sqrt(meanSq);
      float db;
      if (rms < kSilenceRmsEpsilon) {
        db = kFloorDb;
      } else {
        db = static_cast<float>(20.0 * std::log10(rms / kInt16FullScale));
      }
      emit(db);
      sumSq_ = 0.0;
      count_ = 0;
    }
  }
}

void DbWindowAccumulator::emit(float db) {
  if (emitFn_ != nullptr) {
    emitFn_(db, emitUser_);
  }
}

}  // namespace ndk_audio
