# 빌드 게이트 학습 (#1 통과, 2026-05-30) — 모든 워커 필독

## 정식 작업 경로 = `E:\ndk` (원본 한글 경로는 빌드 불가, 보관용)
- 모든 빌드/소스/git 작업은 `E:\ndk`에서. Flutter 루트 = `E:\ndk\app`.
- 계획서: `E:\ndk\.omc\plans\ndk-oboe-ffi-demo.md`.

## 빌드를 막던 3겹 문제 + 해결 (재발 방지)
1. **한글 경로 mojibake**: Gradle JVM `sun.jnu.encoding=MS949`가 cmake에 경로를 깨서 전달 → cmake 크래시. JDK17에서 덮어쓰기 불가. → **ASCII 경로 `E:\ndk`로 이전**해 해결.
2. **NDK r28 legacy toolchain 크래시(0xC0000409 STACK_BUFFER_OVERRUN)**: NDK 27+에서 legacy toolchain file 제거됐는데 AGP가 호출 → cmake가 컴파일러 ABI 검출 단계에서 자폭. → `build.gradle.kts`의 cmake arguments에 **`-DANDROID_USE_LEGACY_TOOLCHAIN_FILE=OFF`** 추가로 해결.
3. **CMakeLists PROJECT_ROOT 경로 오류**: `..` 7단계(→`E:\`) 였음. cpp→...→flutter root→프로젝트루트 = **6단계**(→`E:\ndk`). Oboe를 `E:\third_party`에서 찾다 실패하던 것 수정.
4. (부수) **CMake 3.22.1 → 3.31.6**: NDK28 unified toolchain 안정성. `sdkmanager "cmake;3.31.6"` 설치 완료. build.gradle.kts `version="3.31.6"`.

## 검증된 빌드/실행 절차 (Windows PowerShell)
- 환경: `$env:ANDROID_HOME` 빈 새 셸이면 절대경로 사용. SDK = `C:\Users\PC\AppData\Local\Android\Sdk`.
- 호스트 gtest(순수 로직, #2): VS BuildTools 2019 `vcvars64.bat` 활성화 + SDK `cmake\3.31.6\bin\ninja.exe`. (호스트 빌드는 한글 경로도 됐었지만 E:\ndk에서 진행.)
- Android APK 빌드: `Set-Location E:\ndk\app; flutter build apk --debug --target-platform android-x64` (BUILD SUCCESSFUL 확인됨, 31s).
- 산출물: `E:\ndk\app\build\app\outputs\flutter-apk\app-debug.apk`, `libndk_audio.so`(3 ABI, Oboe static 링크).
- 에뮬레이터: AVD `PlateScan_API_29` (emulator-5554, x86_64, 마이크 ON). adb=`C:\Users\PC\AppData\Local\Android\Sdk\platform-tools\adb.exe`.
- 설치/실행/로그: `adb -s emulator-5554 install -r <apk>` → `am start -n com.example.noise_meter/.MainActivity` → `adb -s emulator-5554 logcat -d`.
- 패키지: `com.example.noise_meter`. 네이티브 로그 태그: `ndk_audio`.

## 게이트 통과 증거
- 호스트 gtest 6/6 green (computeDb, 구형/DC 0/-20 dBFS, 사인파 -3.01 C1검증, 무음 -100, null).
- `I ndk_audio: engine_ping: Oboe linked OK. OBOE_VERSION_NUMBER=17367043 (1.9.3)`.

## 주의 (후속 작업)
- `--target-platform android-x64`를 줘도 3 ABI 모두 빌드됨(Flutter 동작). 게이트엔 무방하나, 반복 빌드 시간 줄이려면 x86_64만 빌드 옵션 검토 가능(필수 아님).
- engine_jni.cpp가 FFI 진입점. native 코어(`E:\ndk\native`)는 호스트/안드로이드 공유, 안드로이드 빌드 시 `NDK_AUDIO_BUILD_TESTS=OFF`.

## #2 학습 (네이티브 순수 로직, 2026-05-30 완료)
- **cmake 버전은 컨텍스트별로 분리**: Android NDK 빌드 = **3.31.6 + legacy OFF**, **호스트 MSVC 빌드 = 3.22.1**. cmake 3.31.6은 MSVC 2019 호스트 configure에서 0xC0000409 크래시 → 호스트는 반드시 3.22.1 사용.
- 호스트 빌드 명령: `cmd /c '"<vcvars64.bat>" && "<sdk>\cmake\3.22.1\bin\cmake.exe" -S E:\ndk\native -B E:\ndk\native\build-host -G Ninja -DCMAKE_MAKE_PROGRAM="<sdk>\cmake\3.22.1\bin\ninja.exe" -DNDK_AUDIO_BUILD_TESTS=ON && cmake --build ...'`. vcvars 경로는 vswhere `-property installationPath` + `\VC\Auxiliary\Build\vcvars64.bat`.
- 모듈 완성: db_calculator, **db_window_accumulator(C2: sumSq 러닝합, carry-over)**, ring_buffer(워커소유 pre-roll), spsc_queue(lock-free), wav_writer(16bit PCM). **호스트 gtest 21개 전부 green** (C1/C2/**C3 가변청크 등가성**/SPSC 2스레드 100k 무손실 포함).
- **TSan 한계**: MSVC는 ThreadSanitizer 미지원(clang 전용). SPSC race 검증은 2스레드 100k 무손실+순서 테스트로 대체. (Android clang 빌드에서 TSan 추가 가능 — 선택.)
- 다음(#3): native 코어를 Android 엔진(engine_jni.cpp/Oboe 콜백/저장 워커)에 통합. 콜백=DbWindowAccumulator.push + SPSC push만, 워커=큐 소비+RingBuffer 소유+상태머신.
