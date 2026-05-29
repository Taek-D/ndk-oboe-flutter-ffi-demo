// db_calculator.h
//
// stateless RMS -> dBFS 변환. 계획서 §5-1 / C1.
// 기준: 0 dBFS = int16 풀스케일(32767) 진폭의 구형/DC. (사인파는 -3.01 dBFS)
#ifndef NDK_AUDIO_DB_CALCULATOR_H_
#define NDK_AUDIO_DB_CALCULATOR_H_

#include <cstdint>

namespace ndk_audio {

// count개 int16 샘플의 RMS를 dBFS로 변환한다.
//   - rms < kSilenceRmsEpsilon(무음) 또는 count <= 0 -> kFloorDb(-100) 클램프.
//   - 그 외: 20 * log10(rms / kInt16FullScale).
// stateless: 윈도우 경계 정렬은 호출자(DbWindowAccumulator)가 책임진다.
float computeDb(const int16_t* samples, int32_t count);

}  // namespace ndk_audio

#endif  // NDK_AUDIO_DB_CALCULATOR_H_
