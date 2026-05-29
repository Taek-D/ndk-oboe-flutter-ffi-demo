// db_calculator.cpp
#include "db_calculator.h"

#include <cmath>

#include "audio_constants.h"

namespace ndk_audio {

float computeDb(const int16_t* samples, int32_t count) {
  if (samples == nullptr || count <= 0) {
    return kFloorDb;
  }

  // sumSq 러닝합(double 정밀도). 풀스케일 6000샘플도 32767^2*6000 ~= 6.4e12 로
  // double 정수 정밀도(2^53) 내에서 무손실.
  double sumSq = 0.0;
  for (int32_t i = 0; i < count; ++i) {
    const double s = static_cast<double>(samples[i]);
    sumSq += s * s;
  }

  const double rms = std::sqrt(sumSq / static_cast<double>(count));
  if (rms < kSilenceRmsEpsilon) {
    return kFloorDb;
  }

  const double db = 20.0 * std::log10(rms / kInt16FullScale);
  return static_cast<float>(db);
}

}  // namespace ndk_audio
