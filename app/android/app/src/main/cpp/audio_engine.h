// audio_engine.h
//
// Oboe 입력 스트림 + 오디오 콜백 + 저장 워커 스레드를 묶는 엔진(#3).
//
// 스레드 모델(계획서 C4-R / P3):
//   - 오디오 콜백(onAudioReady): DbWindowAccumulator.push + PCM 블록 SPSC push "만".
//     malloc/lock/file/log 금지. RingBuffer 직접 미접근.
//   - 저장 워커: SPSC 큐 소비 → 자기 소유 RingBuffer(pre-roll)에 기록.
//     IDLE/CAPTURING 상태머신. 트리거 시 pre-roll 스냅샷 + post-roll append → WAV.
//     연속 트리거는 post-roll 연장(병합). stop 시 in-flight finalize 후 종료.
#ifndef NDK_AUDIO_AUDIO_ENGINE_H_
#define NDK_AUDIO_AUDIO_ENGINE_H_

#include <oboe/Oboe.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "db_window_accumulator.h"
#include "ring_buffer.h"
#include "spsc_queue.h"

namespace ndk_audio {

// engine → 외부(Dart NativeCallable 등)로 125ms마다 dB를 전달하는 콜백 타입.
using DbCallbackFn = void (*)(float db, void* user);

// 콜백 → 워커로 넘기는 고정 크기 PCM 블록(무할당 복사). 16bit mono.
struct PcmBlock {
  static constexpr int32_t kCapacity = 2048;  // 스택/복사 비용 억제(콜백 numFrames 분할).
  int16_t data[kCapacity];
  int32_t count;
};

class AudioEngine : public oboe::AudioStreamDataCallback {
 public:
  AudioEngine();
  ~AudioEngine() override;

  // 제어 경로(콜백 밖)에서 설정.
  void setOutputDir(const std::string& dir);
  void setThreshold(float thresholdDb);
  void setDbCallback(DbCallbackFn fn, void* user);
  float latestDb() const { return latestDb_.load(std::memory_order_relaxed); }

  // 스트림 오픈+시작. 48000 거부 시 false(M3). 권한은 Dart에서 선확보.
  bool start();
  void stop();

  // oboe::AudioStreamDataCallback — 오디오(실시간) 스레드에서 호출됨.
  oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData,
                                        int32_t numFrames) override;

 private:
  static void onDbEmitThunk(float db, void* user);
  void onDbEmit(float db);  // 오디오 스레드에서 호출(DbWindowAccumulator 방출).
  void workerLoop();        // 저장 워커 스레드.
  void finalizeCapture();   // 워커 전용.

  std::shared_ptr<oboe::AudioStream> stream_;

  // 콜백 경로 객체.
  DbWindowAccumulator accumulator_;
  SpscQueue<PcmBlock> pcmQueue_;

  // 콜백/제어 공유 atomics.
  std::atomic<float> thresholdDb_;
  std::atomic<bool> triggerPending_;
  std::atomic<float> latestDb_;
  std::atomic<bool> running_;

  // 진단 카운터(무로그, atomic — 콜백 P3 준수). stop 시 1회 로그.
  std::atomic<int64_t> framesProcessed_;
  std::atomic<int32_t> triggerCount_;

  // dB 외부 콜백(제어 스레드 등록, 오디오 스레드 호출 — listener는 enqueue라 안전).
  DbCallbackFn dbCallback_;
  void* dbCallbackUser_;

  // 워커 스레드 전용 상태.
  std::thread worker_;
  RingBuffer preroll_;            // 워커 단독 소유 pre-roll(C4-R).
  std::vector<int16_t> captureBuf_;
  int32_t postrollRemaining_;
  bool capturing_;
  std::string outputDir_;
  int32_t fileIndex_;
};

}  // namespace ndk_audio

#endif  // NDK_AUDIO_AUDIO_ENGINE_H_
