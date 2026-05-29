// audio_constants.h
//
// 단일 진실 소스(single source of truth)로서의 오디오 상수.
// 계획서 §3 Guardrails / M3: 모든 윈도우/pre-roll/post-roll 크기는
// kSampleRate(48000Hz 강제)에서 파생한다. 매직넘버 직접 기입 금지.
#ifndef NDK_AUDIO_AUDIO_CONSTANTS_H_
#define NDK_AUDIO_AUDIO_CONSTANTS_H_

#include <cstdint>

namespace ndk_audio {

// 강제 샘플레이트. 스트림이 이 값을 거부하면 start를 거부한다(폴백 리샘플 안 함).
constexpr int32_t kSampleRate = 48000;

// 채널 수(mono 고정).
constexpr int32_t kChannelCount = 1;

// dB 연산/방출 윈도우 = 125ms(8Hz). 6000 샘플.
constexpr int32_t kWindowMillis = 125;
constexpr int32_t kWindowSamples = kSampleRate * kWindowMillis / 1000;  // 6000

// pre-roll / post-roll = 각 3초.
constexpr int32_t kPrerollSeconds = 3;
constexpr int32_t kPostrollSeconds = 3;
constexpr int32_t kPrerollSamples = kSampleRate * kPrerollSeconds;    // 144000
constexpr int32_t kPostrollSamples = kSampleRate * kPostrollSeconds;  // 144000

// 단일 이벤트 WAV 최대 길이. 연속 트리거가 끊임없이 병합되어 캡처 버퍼가
// 무한 성장하는 것을 막는다(장시간 메모리 안정성 — M2). 도달 시 강제 finalize.
constexpr int32_t kMaxCaptureSeconds = 30;
constexpr int32_t kMaxCaptureSamples = kSampleRate * kMaxCaptureSeconds;  // 1,440,000

// 무음 판정 임계: RMS가 이보다 작으면 floor dB로 클램프.
constexpr double kSilenceRmsEpsilon = 1e-7;

// 무음/플로어 dB 값.
constexpr float kFloorDb = -100.0f;

// int16 풀스케일(0 dBFS 기준값).
constexpr double kInt16FullScale = 32767.0;

}  // namespace ndk_audio

#endif  // NDK_AUDIO_AUDIO_CONSTANTS_H_
