# NDK-Oboe 오디오 캡처 + Flutter Dart FFI 데모

Android **NDK C/C++** 로 마이크 Raw Audio를 실시간 캡처하고, **Oboe**(AAudio)로 0.125초(125ms) 주기 dB를 연산하며, 임계 초과 이벤트의 전후 구간을 **순환 버퍼**로 WAV 저장하고, **Dart FFI**(NativeCallable)로 Flutter UI에 연동한 데모입니다. 추가로 같은 WiFi의 PC/모바일에서 접속하는 **React 모니터링 웹 + 기기 내장 로컬 웹서버**(실시간 폴링·원격 제어·WAV 스트리밍)를 포함합니다.

> 목적: "NDK C++ 오디오 코드를 직접 작성하고 Flutter와 FFI로 연결할 수 있다"를 **동작하는 코드**로 증명하는 포트폴리오. (dB SPL 절대값 정확도·AGC 우회는 비범위 — 아래 "한계" 참조.)

---

## ⏱ 30초 검증 4포인트

처음 보는 사람이 30초 안에 핵심 역량을 확인할 수 있는 코드 위치입니다.

| # | 무엇 | 파일 위치 |
|---|---|---|
| 1 | **Oboe 입력 스트림 설정** (I16/mono/48kHz/LowLatency, Exclusive→Shared 폴백, 48kHz 강제) | [`app/android/app/src/main/cpp/audio_engine.cpp`](app/android/app/src/main/cpp/audio_engine.cpp) — `AudioEngine::start()` |
| 2 | **dB 연산** (RMS→dBFS + 가변 numFrames를 6000샘플 윈도우로 누산) | [`native/src/db_calculator.cpp`](native/src/db_calculator.cpp) `computeDb()` + [`native/src/db_window_accumulator.cpp`](native/src/db_window_accumulator.cpp) (sumSq 러닝합, carry-over) |
| 3 | **FFI 브릿지** (NativeCallable.listener dB 콜백 + export) | [`app/android/app/src/main/cpp/engine_jni.cpp`](app/android/app/src/main/cpp/engine_jni.cpp) (export) + [`app/lib/audio_engine.dart`](app/lib/audio_engine.dart) (`NativeCallable.listener`) |
| 4 | **콜백/워커 스레드 분리** (오디오 콜백 = 누산+큐push만 / 워커 = RingBuffer 소유+저장) | [`audio_engine.cpp`](app/android/app/src/main/cpp/audio_engine.cpp) — `onAudioReady()` vs `workerLoop()` |

---

## 아키텍처

```
[마이크] → Oboe Input(AAudio, 48kHz I16 mono, 실시간 콜백 스레드)
   │
   ▼ onAudioReady() — 콜백 스레드: 무할당/무락/무파일I/O (P3)
   ├─ DbWindowAccumulator.push  → 6000샘플(125ms)마다 dBFS 1회 방출 → FFI 콜백
   └─ PcmBlock → SPSC lock-free 큐
            │
            ▼ 저장 워커 스레드(단일 소비자)
            ├─ RingBuffer(pre-roll 3초) 단독 소유 — wrap-around
            └─ 임계 초과 시: pre-roll 스냅샷 + post-roll 3초 → WAV
               (IDLE/CAPTURING 상태머신, 연속 트리거 병합, 30초 cap)
   │  Dart FFI
   ▼ NativeCallable.listener (isolate-safe enqueue, 125ms throttle)
[Flutter UI] dB 게이지 / 임계 슬라이더 / 이벤트 로그 / WAV 재생
```

**핵심 설계 원칙**: 오디오 콜백 스레드에서는 절대 블로킹 작업(malloc/lock/file I/O/log)을 하지 않는다. 콜백은 누산과 lock-free 큐 push만 하고, RingBuffer 소유·파일 저장은 워커 스레드가 전담한다. (장시간 측정 안정성의 핵심)

---

## 빌드 / 실행

### 사전 요구
- Flutter (Dart 3.11+), Android SDK, **NDK r28**(`28.2.x`), **CMake 3.31.x**(Android 빌드용), Android Studio 또는 VS Build Tools(호스트 테스트용)
- Android Emulator: **API 26+ x86_64**, 마이크 입력 활성화(Virtual microphone uses host audio input)

### ⚠️ 클론 시 필수 — submodule
Oboe를 소스로 `add_subdirectory` 하므로 submodule을 반드시 함께 받아야 합니다:
```bash
git clone --recursive <repo-url>
# 또는 클론 후:
git submodule update --init --recursive
```
누락 시 `Oboe submodule not found` 빌드 에러가 납니다.

### ⚠️ 경로 제약 (Windows)
프로젝트 경로에 **한글/공백이 없어야** 합니다. Gradle JVM이 `sun.jnu.encoding=MS949`로 cmake에 경로를 넘길 때 한글이 깨져(mojibake) cmake가 크래시합니다(JDK17에서 우회 불가). **ASCII 영문 경로**(예: `E:\ndk`)에서 빌드하세요.

### Android APK 빌드 + 실행
```bash
cd app
flutter pub get
flutter build apk --debug --target-platform android-x64
flutter install   # 또는 adb install -r build/app/outputs/flutter-apk/app-debug.apk
```
앱에서 "측정 시작" → 마이크 권한 허용 → 실시간 dBFS 게이지 표시. 임계 슬라이더를 내리면 이벤트가 트리거되어 전후 구간이 WAV로 저장됩니다(목록에서 재생).

### 네이티브 순수 로직 호스트 테스트 (PC, gtest)
```bash
# VS BuildTools vcvars64 활성화 후 (호스트 빌드는 CMake 3.22.1 사용 권장)
cmake -S native -B native/build-host -G Ninja -DNDK_AUDIO_BUILD_TESTS=ON
cmake --build native/build-host
./native/build-host/ndk_audio_tests   # 21 tests
```

---

## 검증 결과

| 항목 | 결과 |
|---|---|
| 호스트 gtest | **21/21 green** (dB 구형/DC 0·-20dBFS, 사인파 -3.01dBFS, 가변청크 누산 등가성, RingBuffer wrap/overflow, SPSC 2스레드 100k 무손실, WAV 헤더) |
| Oboe 링크 | `engine_ping: Oboe linked OK. OBOE_VERSION_NUMBER=...(1.9.3)` |
| 실시간 캡처 | 에뮬레이터 `stream opened: rate=48000 backend=AAudio`, `framesProcessed` 지속 증가 |
| dB 콜백 | NativeCallable.listener로 UI에 ~8Hz dBFS 전달 |
| 이벤트 WAV | `event_000.wav` — **48000Hz / mono / 16bit**, 전후 구간 정상 저장 |
| 메모리 안정성 | 60초 반복 트리거(워커/저장 부하), Native Heap 증가 **+44KB**(<5MB), 크래시 0 |

### 30분 장시간 안정성 측정 절차 (수동 권장)
```bash
adb shell dumpsys meminfo <pkg> | findstr "Native Heap"   # 시작값 기록
# 측정 시작 + 1~2분마다 임계 초과 트리거를 반복(워커/저장 경로 반복 가동)하며 30분 측정
adb shell dumpsys meminfo <pkg> | findstr "Native Heap"   # 종료값 — 증가 < 5MB 확인
```
연속 트리거가 끊임없이 이어져도 단일 이벤트는 30초에서 강제 finalize되어(`kMaxCaptureSamples`) 캡처 버퍼가 무한 성장하지 않습니다.

---

## 🌐 추가 데모: 로컬 네트워크 + 모니터링 웹

공고의 *"동일 로컬 네트워크(WiFi) 접속 시 기기 내장 로컬 웹서버를 통한 음원 스트리밍 + 실시간 폴링 + 원격 제어"* 요구를 별도로 증명합니다.

- **기기 내장 로컬 HTTP 서버** ([`app/lib/local_server.dart`](app/lib/local_server.dart) — `dart:io`, 추가 패키지 0):
  - `GET /api/status` — 실시간 dB·상태 폴링
  - `POST /api/pause | /api/resume | /api/threshold` — 원격 제어
  - `GET /api/events` — 이벤트 WAV 목록
  - `GET /api/audio/<name>` — WAV 스트리밍(**HTTP Range** → 브라우저 seek 지원)
- **React 모니터링 웹** ([`monitor-web/src/App.jsx`](monitor-web/src/App.jsx) — Vite + React):
  - 기기 IP 접속 → 1초 폴링 dB 게이지 → 일시정지/재개/임계 원격 제어 → 이벤트 WAV `<audio>` 스트리밍 재생

### 실행
```bash
cd monitor-web && npm install && npm run dev   # http://localhost:5173
# 앱 화면 상단에 표시되는 "모니터링 웹 접속 주소"(http://<기기IP>:8080)를 웹 입력란에 입력
```

### 검증 (에뮬레이터 + adb forward)
| 호출 | 결과 |
|---|---|
| `GET /api/status` | `{running:true, db:-78.4, threshold, eventCount}` (curl + React 1초 폴링) |
| `POST /api/pause` / `/api/resume` | `running:false` / `running:true` (원격 제어) |
| `GET /api/audio/<name>` | **HTTP 206 Partial Content**, `audio/wav`, `Accept-Ranges: bytes` |
| React 웹 E2E | 실시간 dB 게이지·상태·원격 제어 동작 — [monitor-web-e2e.png](monitor-web-e2e.png) |

> 참고: 로컬 서버는 데모 범위로 인증이 없습니다(동일 LAN 가정). 실제 배포 시 토큰/페어링 추가 필요.

---

## 한계 (정직한 명시)

- **에뮬레이터 마이크 경로**: 호스트 PC 마이크를 사용합니다. 호스트 OS/에뮬레이터 단의 처리(AGC/노이즈 억제)가 끼어들 수 있어 "AGC 없는 깨끗한 입력"이 보장되지 않습니다. 실기기/기종별 AGG 우회 검증은 본 데모 범위 밖입니다(계약 후 클라이언트 제공 장비로 진행 예정).
- **"유사 SPL"**: 표시 dB는 dBFS 기반입니다. 실제 dB SPL 절대값은 마이크별 캘리브레이션 오프셋(클라이언트 제공)이 더해져야 하며, 본 데모는 보정하지 않습니다.
- **빌드 시스템 선택**: Flutter `native-assets`(build hooks) 대신 **전통 Android Gradle + CMake(externalNativeBuild) + Oboe 소스 `add_subdirectory`** 를 채택했습니다. 이유: Oboe(prefab/소스)의 1급 통합 경로이고, lldb 디버깅·예제·재현성에서 검증된 경로이기 때문입니다. (의도적 선택 — `native-assets`는 Android+Oboe 조합에서 정상 경로가 아님)
- **외장 마이크 / 캘리브레이션 동기화 / Firebase·모니터링 웹**: 본 데모 비범위.

---

## 기술 스택
NDK r28 · Oboe 1.9.3(소스 빌드) · CMake(Android 3.31 / 호스트 3.22) · C++17 · Flutter(Dart 3.11) · Dart FFI(NativeCallable) · GoogleTest
