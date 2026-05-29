// audio_engine.cpp
#include "audio_engine.h"

#include <android/log.h>

#include <chrono>
#include <cstdio>
#include <cstring>

#include "audio_constants.h"
#include "wav_writer.h"

#define LOG_TAG "ndk_audio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace ndk_audio {

AudioEngine::AudioEngine()
    : pcmQueue_(128),  // 128 * 2048 샘플 ≈ 5.5초 분량(pre-roll 3초보다 큼).
      thresholdDb_(-20.0f),
      triggerPending_(false),
      latestDb_(kFloorDb),
      running_(false),
      dbCallback_(nullptr),
      dbCallbackUser_(nullptr),
      framesProcessed_(0),
      triggerCount_(0),
      preroll_(kPrerollSamples),
      postrollRemaining_(0),
      capturing_(false),
      fileIndex_(0) {
  captureBuf_.reserve(static_cast<size_t>(kPrerollSamples + kPostrollSamples));
}

AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::setOutputDir(const std::string& dir) { outputDir_ = dir; }

void AudioEngine::setThreshold(float thresholdDb) {
  thresholdDb_.store(thresholdDb, std::memory_order_relaxed);
}

void AudioEngine::setDbCallback(DbCallbackFn fn, void* user) {
  dbCallback_ = fn;
  dbCallbackUser_ = user;
}

void AudioEngine::onDbEmitThunk(float db, void* user) {
  static_cast<AudioEngine*>(user)->onDbEmit(db);
}

// 오디오 스레드. DbWindowAccumulator가 125ms(6000샘플)마다 1회 호출.
void AudioEngine::onDbEmit(float db) {
  latestDb_.store(db, std::memory_order_relaxed);
  if (dbCallback_ != nullptr) {
    dbCallback_(db, dbCallbackUser_);  // NativeCallable.listener면 enqueue(non-block).
  }
  if (db > thresholdDb_.load(std::memory_order_relaxed)) {
    triggerPending_.store(true, std::memory_order_relaxed);
    triggerCount_.fetch_add(1, std::memory_order_relaxed);
  }
}

// 오디오(실시간) 스레드. 누산 + 큐 push만. 무할당/무락/무로그.
oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream* /*stream*/,
                                                   void* audioData, int32_t numFrames) {
  const int16_t* in = static_cast<const int16_t*>(audioData);
  framesProcessed_.fetch_add(numFrames, std::memory_order_relaxed);

  // (1) dB 윈도우 누산 → 6000샘플마다 onDbEmit.
  accumulator_.push(in, numFrames);

  // (2) PCM을 PcmBlock 단위로 분할해 SPSC 큐에 push(워커가 pre-roll/post-roll 처리).
  int32_t off = 0;
  while (off < numFrames) {
    PcmBlock blk;
    int32_t n = numFrames - off;
    if (n > PcmBlock::kCapacity) n = PcmBlock::kCapacity;
    std::memcpy(blk.data, in + off, static_cast<size_t>(n) * sizeof(int16_t));
    blk.count = n;
    pcmQueue_.push(blk);  // full이면 drop(워커 지연) — 데모에선 허용.
    off += n;
  }
  return oboe::DataCallbackResult::Continue;
}

// 저장 워커 스레드. 큐 소비 + RingBuffer(pre-roll) 기록 + 상태머신.
void AudioEngine::workerLoop() {
  PcmBlock blk;
  while (true) {
    if (!pcmQueue_.pop(blk)) {
      if (!running_.load(std::memory_order_acquire)) break;  // 종료(잔여까지 비운 뒤).
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      continue;
    }

    // 항상 pre-roll 링버퍼에 기록(콜백은 직접 안 만짐 — C4-R).
    preroll_.write(blk.data, blk.count);

    // 트리거 소비.
    if (triggerPending_.exchange(false, std::memory_order_relaxed)) {
      if (!capturing_) {
        // IDLE → CAPTURING: 직전 pre-roll(최대 3초) 스냅샷을 WAV 앞부분으로.
        capturing_ = true;
        captureBuf_.resize(static_cast<size_t>(kPrerollSamples));
        const int32_t got = preroll_.snapshot(captureBuf_.data(), kPrerollSamples);
        captureBuf_.resize(static_cast<size_t>(got));
        postrollRemaining_ = kPostrollSamples;
      } else {
        // CAPTURING 중 재트리거 → post-roll 연장(병합, 새 WAV 안 만듦).
        postrollRemaining_ = kPostrollSamples;
      }
    }

    // CAPTURING이면 이 블록을 post-roll로 append.
    if (capturing_) {
      captureBuf_.insert(captureBuf_.end(), blk.data, blk.data + blk.count);
      postrollRemaining_ -= blk.count;
      // post-roll 소진 OR 단일 이벤트 최대 길이(30초) 도달 시 finalize.
      // 후자는 연속 트리거 무한 병합 시 캡처 버퍼 무한 성장을 막는다(M2 메모리 안정).
      if (postrollRemaining_ <= 0 ||
          static_cast<int32_t>(captureBuf_.size()) >= kMaxCaptureSamples) {
        finalizeCapture();
      }
    }
  }
  // stop 경계: 수집 중이던 이벤트를 flush.
  if (capturing_) {
    finalizeCapture();
  }
}

void AudioEngine::finalizeCapture() {
  if (!captureBuf_.empty() && !outputDir_.empty()) {
    char path[768];
    std::snprintf(path, sizeof(path), "%s/event_%03d.wav", outputDir_.c_str(), fileIndex_);
    const bool ok = writeWav(path, captureBuf_.data(),
                             static_cast<int32_t>(captureBuf_.size()), kSampleRate, 1);
    LOGI("event WAV %s: %s (%zu samples)", path, ok ? "saved" : "FAILED",
         captureBuf_.size());
    ++fileIndex_;
  }
  captureBuf_.clear();
  capturing_ = false;
  postrollRemaining_ = 0;
}

bool AudioEngine::start() {
  if (running_.load(std::memory_order_acquire)) return true;

  oboe::AudioStreamBuilder b;
  b.setDirection(oboe::Direction::Input)
      ->setFormat(oboe::AudioFormat::I16)
      ->setChannelCount(oboe::ChannelCount::Mono)
      ->setSampleRate(kSampleRate)
      ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
      ->setSharingMode(oboe::SharingMode::Exclusive)
      ->setDataCallback(this);

  oboe::Result r = b.openStream(stream_);
  if (r != oboe::Result::OK) {
    LOGE("openStream(Exclusive) failed: %s — retry Shared", oboe::convertToText(r));
    b.setSharingMode(oboe::SharingMode::Shared);
    r = b.openStream(stream_);
    if (r != oboe::Result::OK) {
      LOGE("openStream(Shared) failed: %s", oboe::convertToText(r));
      return false;
    }
  }

  // M3: 48000 강제. 다른 레이트면 거부(폴백 리샘플 안 함).
  const int32_t actualRate = stream_->getSampleRate();
  if (actualRate != kSampleRate) {
    LOGE("sample rate %d != %d — refusing start (M3)", actualRate, kSampleRate);
    stream_->close();
    stream_.reset();
    return false;
  }
  LOGI("stream opened: rate=%d sharing=%d perf=%d backend=%d", actualRate,
       static_cast<int>(stream_->getSharingMode()),
       static_cast<int>(stream_->getPerformanceMode()),
       static_cast<int>(stream_->getAudioApi()));

  // 콜백 경로 상태 초기화.
  accumulator_.reset();
  accumulator_.setEmitCallback(&AudioEngine::onDbEmitThunk, this);
  triggerPending_.store(false, std::memory_order_relaxed);
  latestDb_.store(kFloorDb, std::memory_order_relaxed);
  preroll_.clear();
  captureBuf_.clear();
  capturing_ = false;
  postrollRemaining_ = 0;

  running_.store(true, std::memory_order_release);
  worker_ = std::thread(&AudioEngine::workerLoop, this);

  r = stream_->requestStart();
  if (r != oboe::Result::OK) {
    LOGE("requestStart failed: %s", oboe::convertToText(r));
    stop();
    return false;
  }
  LOGI("engine started (threshold=%.1f dBFS)", thresholdDb_.load());
  return true;
}

void AudioEngine::stop() {
  // 콜백 먼저 중단(스트림 정지) → 워커가 잔여 큐 처리 + in-flight finalize → join.
  if (stream_) {
    stream_->requestStop();
    stream_->close();
    stream_.reset();
  }
  running_.store(false, std::memory_order_release);
  if (worker_.joinable()) {
    worker_.join();
  }
  LOGI("engine stopped: framesProcessed=%lld triggers=%d filesWritten=%d",
       static_cast<long long>(framesProcessed_.load()), triggerCount_.load(), fileIndex_);
}

}  // namespace ndk_audio
