// db_window_accumulator.h
//
// C2: Oboe onAudioReady(stream, audioData, numFrames)의 numFrames는 콜백마다
// 가변이고 kWindowSamples(6000)의 약수/배수 보장이 없다. 이 누산기가 윈도우
// 경계 정렬을 책임진다.
//
// 핵심 설계(P3 정신 + sumSq 러닝합):
//   - 샘플 버퍼를 carry-over 하지 않는다. 대신 "현재 윈도우에 쌓인
//     sumSq(부분제곱합)와 샘플 개수"만 스칼라로 유지한다 → O(n) 1패스, 무할당.
//   - push로 들어온 샘플을 하나씩 누산하다 개수가 kWindowSamples에 도달하면
//     그 윈도우의 dB(=10*log10(sumSq/count/fullscale^2))를 1회 방출하고
//     sumSq/count를 0으로 리셋(잔여는 다음 윈도우로 자연히 이어짐).
//   - 한 번의 push가 여러 윈도우를 채우면 윈도우마다 반복 방출한다.
//
// 방출은 콜백(가벼운 함수 포인터)로 전달한다. 콜백 경로에서 동적 할당/락/로그
// 금지(계획서 P3) — 이 클래스는 그 제약을 지킨다(스칼라 누산만).
#ifndef NDK_AUDIO_DB_WINDOW_ACCUMULATOR_H_
#define NDK_AUDIO_DB_WINDOW_ACCUMULATOR_H_

#include <cstdint>

namespace ndk_audio {

// 윈도우 dB가 방출될 때 호출되는 콜백 타입.
//   db: 해당 윈도우의 dBFS(무음이면 kFloorDb).
//   user: push 호출자가 등록한 불투명 포인터.
using DbEmitFn = void (*)(float db, void* user);

class DbWindowAccumulator {
 public:
  DbWindowAccumulator();

  // 방출 콜백 등록(start 이전, 콜백 경로 밖에서 설정).
  void setEmitCallback(DbEmitFn fn, void* user);

  // numFrames개 int16 샘플(mono)을 누산한다. 윈도우(kWindowSamples)가 찰 때마다
  // 등록된 콜백으로 dB를 1회 방출한다. 무할당/O(numFrames).
  void push(const int16_t* frames, int32_t numFrames);

  // 누산 상태 초기화(잔여 샘플/부분합 버림). start/stop 경계에서 사용.
  void reset();

  // --- 테스트/관측용 접근자 ---
  // 현재 윈도우에 쌓인(아직 방출 안 된) 잔여 샘플 수(carry-over).
  int32_t pendingSampleCount() const { return count_; }

 private:
  void emit(float db);

  double sumSq_;     // 현재 윈도우의 부분 제곱합.
  int32_t count_;    // 현재 윈도우에 쌓인 샘플 수(< kWindowSamples).
  DbEmitFn emitFn_;
  void* emitUser_;
};

}  // namespace ndk_audio

#endif  // NDK_AUDIO_DB_WINDOW_ACCUMULATOR_H_
