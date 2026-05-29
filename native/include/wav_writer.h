// wav_writer.h
//
// 16bit PCM mono WAV 파일 작성. 헤더에 실제 sampleRate를 기록한다(M3: 48000 강제이나
// 헤더는 전달된 실제 값 사용). 저장 워커 스레드에서만 호출(콜백 경로 금지).
#ifndef NDK_AUDIO_WAV_WRITER_H_
#define NDK_AUDIO_WAV_WRITER_H_

#include <cstdint>
#include <string>

namespace ndk_audio {

// samples(int16, numSamples개)를 16bit PCM WAV로 path에 기록한다.
// 성공 시 true. 잘못된 인자(nullptr/음수/0 sampleRate 등)면 false.
bool writeWav(const std::string& path, const int16_t* samples, int32_t numSamples,
              int32_t sampleRate, int16_t channels = 1);

}  // namespace ndk_audio

#endif  // NDK_AUDIO_WAV_WRITER_H_
