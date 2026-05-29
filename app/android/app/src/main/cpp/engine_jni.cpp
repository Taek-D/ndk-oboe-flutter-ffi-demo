// engine_jni.cpp
//
// FFI 진입점(extern "C"). Dart(#4)에서 DynamicLibrary.lookup으로 호출한다.
//   - engine_ping(): Oboe 링크 검증(Phase 0 게이트).
//   - engine_start/stop/set_threshold/set_db_callback/set_output_dir/get_db: 엔진 제어(#3).
//
// 로깅은 제어 경로(콜백 밖)에서만. 단일 전역 엔진(프로세스 수명).

#include <android/log.h>

#include <oboe/Oboe.h>

#include "audio_engine.h"

#define LOG_TAG "ndk_audio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {
ndk_audio::AudioEngine g_engine;
}  // namespace

extern "C" {

// Phase 0 게이트: Oboe 심볼 링크 검증. 반환=OBOE_VERSION_NUMBER.
int engine_ping() {
  const int version = OBOE_VERSION_NUMBER;
  oboe::AudioStreamBuilder builder;
  builder.setDirection(oboe::Direction::Input)
      ->setFormat(oboe::AudioFormat::I16)
      ->setChannelCount(oboe::ChannelCount::Mono)
      ->setSampleRate(48000);
  LOGI("engine_ping: Oboe linked OK. OBOE_VERSION_NUMBER=%d (%s)", version,
       OBOE_VERSION_TEXT);
  return version;
}

// 저장 디렉토리(Dart가 getExternalFilesDir 경로 전달). start 이전에 호출.
void engine_set_output_dir(const char* dir) {
  if (dir != nullptr) {
    g_engine.setOutputDir(dir);
  }
}

// 이벤트 트리거 임계(dBFS).
void engine_set_threshold(float db) { g_engine.setThreshold(db); }

// engine → Dart dB 콜백 등록. fn은 NativeCallable.listener의 네이티브 함수 포인터.
// 125ms마다 1회 호출된다(throttle은 DbWindowAccumulator가 보장).
void engine_set_db_callback(void (*fn)(float, void*), void* user) {
  g_engine.setDbCallback(reinterpret_cast<ndk_audio::DbCallbackFn>(fn), user);
}

// 스트림 시작. 성공 1, 실패 0(48000 거부 등). RECORD_AUDIO 권한은 Dart에서 선확보.
int engine_start() { return g_engine.start() ? 1 : 0; }

// 스트림 정지 + in-flight 이벤트 flush + 워커 join.
void engine_stop() { g_engine.stop(); }

// 최신 125ms 윈도우 dBFS(폴링용 보조; 주 경로는 콜백).
float engine_get_db() { return g_engine.latestDb(); }

}  // extern "C"
