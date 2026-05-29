# NDK-Oboe 오디오 캡처 + Flutter Dart FFI 데모 — 구현 계획서 (RALPLAN)

> 상태: **PENDING APPROVAL** (RALPLAN 합의 완료 — Architect 승인 권장 + Critic VERDICT: APPROVE, iteration 3)
> 작성일: 2026-05-29 (합의 도달 2026-05-30)
> 모드: DELIBERATE (신규 C++/NDK + 미검증 빌드 통합 = 고리스크)
> 기획서 출처: `NDK-Oboe-Audio-Demo-기획서.md`
> 목적: "NDK C++ 오디오 코드를 작성하고 Flutter와 FFI로 연결할 수 있다"를 동작하는 코드로 증명

---

## 0. Context (배경 요약)

- 이 산출물은 정확도(AGC 우회/캘리브레이션) 증명이 아니라 **기술 역량 증명용 포트폴리오**다. 완벽한 dB SPL이 아니라 "엔드투엔드로 동작하는 NDK↔FFI 파이프라인"이 합격 기준이다.
- 개발자는 Python + Unity C# 경험 보유, **C++/NDK는 신규**. 따라서 계획은 (1) 빠른 피드백 루프(PC 호스트 단위 테스트), (2) 가장 큰 두 가지 빌드 통합 리스크의 조기 검증을 최우선으로 설계한다.
- 환경: Windows 11 호스트 + Android Emulator(API 26+ x86_64, 마이크 ON), Flutter 3.44, NDK r28, Oboe 1.9.x, CMake 3.22+, C++17.

### 0-1. 조사로 확정된 사실 (2026-05 기준)

| 사실 | 출처 요지 | 계획 영향 |
|---|---|---|
| Flutter build hooks(`hook/build.dart` + native assets)는 3.38/Dart 3.10부터 **stable** | flutter create --template=package_ffi 권장 경로 | A안 자체는 "실험적"이 아님 |
| build hooks는 `native_toolchain_c`로 **C 소스를 직접 컴파일**하는 데 최적화. OS별 빌드파일(build.gradle/CMakeLists) 작성을 없애는 게 설계 목표 | dart-lang/native, Flutter 공식 문서 | Oboe(prefab AAR/Gradle 의존성)를 native-assets 경로로 끌어오는 건 **정상 경로가 아님 → 리스크** |
| Oboe의 1급 통합 = prefab AAR (Gradle `implementation` + CMake `find_package(oboe)`), 또는 소스 `add_subdirectory(oboe)` | Oboe GettingStarted, Android 공식 | 둘 다 **전통 Android Gradle+CMake(externalNativeBuild)** 토대 위에서 동작 |
| Android 모듈은 CMake 스크립트 1개만 링크 가능(top-level CMakeLists에서 나머지를 dependency로) | Android 공식 | 단일 `CMakeLists.txt`로 우리 코드 + Oboe를 묶어야 함 |

> **결론(핵심 판단):** native-assets(A)는 Linux/Win/iOS C 코드엔 깔끔하지만, **Android + Oboe(prefab) 조합**에서는 전통 Gradle+CMake(B)가 문서·예제·디버깅 측면에서 검증된 경로다. 이 데모는 Android 단일 타깃 + Oboe 외부 의존성이 핵심이므로 **B를 기본(primary), A를 스파이크(탐색)로** 둔다. (상세 비교는 §2 RALPLAN-DR)

---

## 1. Work Objectives (작업 목표)

1. Android 에뮬레이터에서 NDK C++(`libndk_audio.so`)가 빌드·실행되고 Oboe Input 스트림으로 마이크 Raw Audio를 캡처한다.
2. C++ 레벨에서 125ms(8Hz) 주기 RMS→dBFS를 연산하고, 호스트(PC) 단위 테스트로 정확성을 증명한다.
3. 링버퍼(pre-roll 3초) + 라이브 post-roll(3초) 구조로 임계값 초과 이벤트의 전후 6초를 WAV로 저장한다. 저장은 오디오 콜백과 분리된 워커 스레드 + lock-free SPSC 큐로 처리한다.
4. Dart FFI로 엔진 제어(start/stop/setThreshold)와 dB 실시간 콜백(throttle 125ms)을 연결하고 Flutter UI(게이지/슬라이더/이벤트 로그/WAV 재생)로 표시한다.
5. 30분 연속 측정 안정성(크래시/메모리 누수 없음)을 점검하고, GitHub 공개 + README(30초 검증 4포인트 + 환경 한계 명시)를 작성한다.

---

## 2. RALPLAN-DR 요약 (review 전 필수)

### 2-1. Principles (원칙)

1. **증명이 목적이다, 정확도가 아니다.** dB 절대값/AGC는 비범위. "동작하는 코드"를 보이는 데 자원을 집중한다.
2. **리스크를 앞으로 당긴다.** 가장 불확실한 것(빌드 통합)을 Day 1~2에 검증한다. 끝에 가서 막히면 마감을 못 지킨다.
3. **오디오 콜백 스레드는 신성하다.** 콜백에서 file I/O / malloc / lock / 로그 금지. 콜백 책임은 **누산(DbWindowAccumulator) + SPSC 큐 push** 2개로만 한정(링버퍼 write·144000샘플 memcpy도 콜백이 아닌 워커가 수행 — C4-R). 이 한 줄이 장시간 안정성의 90%다.
4. **피드백 루프를 짧게.** 순수 로직(DbCalculator/RingBuffer)은 PC 호스트에서 gtest로 초 단위 검증. 에뮬레이터는 통합 검증에만 쓴다.
5. **불확실은 정직하게.** 검증 안 된 기술 사실은 README/계획에 "검증 필요 + 폴백"으로 표기한다(특히 native-assets, 에뮬레이터 마이크 경로).

### 2-2. Decision Drivers (의사결정 동인 상위 3)

1. **마감 리스크 (D-14, 신규 C++).** 14일 안에 합격 가능한 데모가 나와야 한다. 검증된 경로 우선.
2. **채용 담당자 검증성.** 코드 30초 내 "NDK 직접 작성"이 보여야 한다. 빌드 시스템이 화려할 필요 없음 → 표준적이고 읽기 쉬운 구조 선호.
3. **디버깅 가능성.** 신규 NDK 개발자가 segfault를 lldb로 추적할 수 있어야 한다 → Android Studio 1급 지원(전통 Gradle+CMake)이 유리.

### 2-3. Viable Options (실질 대안 — 최소 2개)

#### 옵션 A: Flutter `package_ffi` + native assets / build hooks (`hook/build.dart`)

- **방식:** `flutter create --template=package_ffi` → `hook/build.dart`에서 `native_toolchain_c`로 C/C++ 컴파일, ffigen으로 바인딩 자동 생성, `@Native()` 자동 resolve.
- **장점:**
  - 2026-05 기준 Flutter 공식 권장·stable 경로(미래지향).
  - OS별 빌드 파일 불필요 → 멀티플랫폼 확장 시 깔끔.
  - 바인딩(ffigen) + 런타임 resolve 자동화로 보일러플레이트 적음.
- **단점 (경계 있는):**
  - **Oboe(prefab AAR/Gradle 의존성) 연동이 native-assets의 정상 경로가 아님** → Oboe를 소스 `add_subdirectory`로 끌어와 `native_toolchain_c`/커스텀 빌드훅에서 컴파일해야 할 가능성. **검증 필요(최대 리스크).**
  - 신규 NDK 개발자가 빌드훅 디버깅 + lldb 연결 시 자료/예제 적음 → 막혔을 때 탈출구가 좁다.
  - dB 콜백(NativeCallable.listener) + native-assets 조합 예제가 희소.

#### 옵션 B: 전통 Android Gradle + CMake(externalNativeBuild) 플러그인 방식 (**Planner 권장 primary**)

- **방식:** Flutter 플러그인/앱의 `android/` 모듈에 `externalNativeBuild { cmake { ... } }` + 표준 `CMakeLists.txt`. Oboe는 **소스 `add_subdirectory(oboe)` 가 primary**(submodule 버전 고정), prefab AAR(`implementation "com.google.oboe:oboe:1.9.x"` + `find_package(oboe)`)은 선택 시연(§9 ADR 참조). Dart는 `DynamicLibrary.open('libndk_audio.so')`로 로드.
- **장점:**
  - **Oboe의 1급 통합 경로**(prefab/add_subdirectory 둘 다 문서·예제 풍부).
  - Android Studio lldb 1급 지원 → segfault 네이티브 스택 추적 쉬움(신규 개발자에게 결정적).
  - 채용 담당자에게 익숙하고 읽기 쉬운 표준 NDK 구조 → "직접 작성" 검증 명확.
  - 막혔을 때 StackOverflow/공식 샘플 다수 → 마감 리스크 최소.
- **단점 (경계 있는):**
  - native-assets 대비 "구식"으로 보일 수 있음(README에서 의도적 선택임을 명시해 상쇄).
  - 빌드파일(build.gradle + CMakeLists) 수기 작성 필요(보일러플레이트 多).
  - 순수 Dart FFI 패키지가 아니라 Android 앱/플러그인 구조에 묶임.

#### 단일안 미채택 근거 (왜 둘 다 살려두는가)

- A를 완전히 버리지 않는 이유: 2026-05 stable이고 채용 어필 요소이며, B 진행 중 막혔을 때의 비교군이 된다.
- B를 기본으로 두는 이유: **이 데모의 핵심 의존성(Oboe)이 전통 Gradle+CMake에 최적화**되어 있고, 신규 NDK 개발자의 디버깅·마감 리스크를 가장 낮춘다.
- **권장 전략:** Day 1에 B로 "Oboe 링크되는 빈 NDK 빌드"를 먼저 성공시키고(Go/No-Go 게이트), 시간이 남으면 Day 6 FFI 단계에서 A를 1~2시간 스파이크로 시도. A가 빠르게 동작하면 채택, 아니면 B 유지. **A에 마감 리스크를 걸지 않는다.**

### 2-4. Pre-mortem (DELIBERATE — 실패 시나리오 3)

1. **"Oboe가 CMake에서 안 링크된다."** (가장 가능성 높음)
   - 원인: submodule 미초기화, STL 불일치(`c++_shared` 누락), NDK r28/Oboe 1.9.x ABI 불일치, x86_64 ABI 빌드 누락.
   - 방어: Day 1-PM을 **빌드 통합 게이트**로 격리하고, 먼저 hello-oboe 공식 샘플로 환경 정상성을 분리 검증(내 코드 vs 환경 切り分け). **소스 `add_subdirectory(oboe)` 가 primary**(버전 레포 고정)이므로 prefab ABI 불일치 리스크는 1차 경로에서 제거. `-DANDROID_STL=c++_shared` + `abiFilters 'x86_64'` 명시로 혼선 축소. (소스가 막히면 prefab 시연 경로로 전환)
2. **"오디오 콜백 스레드에서 간헐 크래시/글리치."**
   - 원인: 콜백 내 lock/alloc/file I/O, RingBuffer race, SPSC 큐 잘못된 메모리 순서.
   - 방어: 콜백은 오직 (RMS 누산 + 큐 push)만 — **RingBuffer는 워커가 소유하므로 콜백은 링버퍼를 만지지 않는다(C4-R)**. 큐는 `std::atomic` acquire/release SPSC. ThreadSanitizer를 **호스트 테스트**에서 켜서 race 조기 검출. Day 9 30분 안정성 테스트로 최종 확인.
3. **"FFI dB 콜백이 Dart에 안 오거나 isolate 크래시."**
   - 원인: `NativeCallable.listener` 미사용(콜백 isolate 위반), throttle 미적용으로 8kHz로 Dart 폭격, 함수 시그니처 불일치.
   - 방어: engine→dart는 반드시 `NativeCallable.listener`. dB는 **네이티브에서 125ms마다 1회만** 큐잉 후 콜백(프레임당 금지). 시그니처는 ffigen 또는 수기 typedef로 양쪽 단일 소스화.

---

## 3. Guardrails (가드레일)

### Must Have

- Oboe Input 스트림은 `AudioFormat::I16`, mono, **48000Hz 고정**, `PerformanceMode::LowLatency` 시도.
  - **샘플레이트 정책 (M3 — 택1 확정):** 48000Hz를 강제한다. 빌드 시 `setSampleRate(48000)` + `setSampleRateConversionQuality` 미사용. 스트림 오픈 후 실제 `stream->getSampleRate()`가 48000이 아니면 **에러 반환 + start 거부**(폴백 리샘플링 안 함). 하드코딩 상수(window/preroll/postroll)는 단일 헤더의 `kSampleRate=48000`에서 파생: `kWindowSamples = kSampleRate * 0.125 = 6000`, `kPrerollSamples = kSampleRate * 3 = 144000`, `kPostrollSamples = kSampleRate * 3 = 144000`. 매직넘버 직접 기입 금지.
  - **SharingMode 폴백 로깅:** `SharingMode::Exclusive` 시도 → 실패 시 Oboe가 `Shared`로 자동 폴백. 실제 채택된 모드/버퍼 크기를 start 시 1회 Logcat 기록(기획서 §5-3과 일관). 콜백 루프 내 로깅은 금지.
- 오디오 콜백(`onAudioReady`) 내부: RMS 누산(DbWindowAccumulator) + 임계 비교 시 `TriggerEvent` push + PcmBlock을 lock-free SPSC 큐에 push **만**. **링버퍼(RingBuffer) write는 콜백이 하지 않는다 — 워커가 큐에서 받아 자기 소유 RingBuffer에 기록**(C4-R 일원화). 콜백이 144000샘플 memcpy 같은 무거운 작업을 하지 않도록 보장(P3 강화).
- dB→Dart 콜백은 125ms(8Hz) throttle. RECORD_AUDIO 런타임 권한은 **Dart에서 획득 후** 네이티브 start 호출.
- 순수 로직(DbCalculator/DbWindowAccumulator/RingBuffer/SPSC 큐/EventStateMachine)은 PC 호스트 gtest/Catch2로 단위 테스트.
- WAV 저장 경로는 app-specific external storage(`getExternalFilesDir(null)`, 예: `/sdcard/Android/data/<pkg>/files/events/`)를 사용 → 권한 추가 없이 `adb pull` 가능, scoped storage 호환.
- README에 환경 한계 3종 명시: (1) 에뮬레이터 호스트 마이크 경로의 OS 처리/AGC 가능성, (2) dBFS+임의 오프셋의 "유사 SPL"임, (3) native-assets 미채택/채택 사유.

### Must NOT Have

- 오디오 콜백에서 `malloc`/`new`/`std::mutex::lock`/`fopen`/`__android_log_print` 호출 금지.
- 프레임당 Dart 콜백 금지(throttle 필수).
- 권한 획득 전 네이티브 스트림 start 금지.
- AGC 우회/캘리브레이션/실기기/외장 마이크/Firebase/모니터링 웹 구현 금지(전부 비범위).
- A안(native-assets)에 마감을 거는 행위 금지(스파이크로만).

---

## 4. Task Flow (작업 흐름 — 단계 게이트)

```
[Phase 0: 빌드 통합 게이트]  Day 1 (AM/PM 분할)   ← 최대 리스크, Go/No-Go
   AM: 호스트 gtest computeDb green
   PM: hello-oboe 환경검증 → 소스 add_subdirectory Oboe 링크 .so + lldb 연결
        │ (성공해야 진행)
        ▼
[Phase 1: 네이티브 코어 (PC 호스트 우선)]  Day 2~5
   Oboe 캡처 → DbCalculator → SPSC 큐 → 저장 워커[ RingBuffer(pre-roll) 소유 + post-roll ] → WAV
   (RingBuffer는 워커 소유 — 콜백은 큐 push만, C4-R. 순수 로직은 PC gtest로, 통합은 에뮬레이터로)
        │
        ▼
[Phase 2: FFI 브릿지]  Day 6~7
   start/stop/setThreshold export + NativeCallable.listener dB 콜백 + RECORD_AUDIO 권한
   (선택) A안 1~2h 스파이크
        │
        ▼
[Phase 3: Flutter UI + 안정성]  Day 8~9
   게이지/슬라이더/이벤트로그/WAV재생 + 30분 안정성 + 메모리 점검
        │
        ▼
[Phase 4: 문서/공개]  Day 10
   README(30초 검증 4포인트 + 한계) + GitHub 공개 + 시연 캡처
```

---

## 5. Detailed TODOs (단계별 상세 + 수용 기준)

### Phase 0 — 빌드 통합 게이트 (Day 1, 분할) [최우선 리스크]

> **Synthesis 반영:** Day 1을 AM/PM으로 분할. AM은 신규 개발자에게 "성취 + 핵심 역량"을 먼저 확보(호스트 gtest green), PM은 환경 정상성을 공식 샘플로 분리 확인한 뒤 내 프로젝트에 Oboe 링크. 리스크 전진은 유지하되 학습곡선을 평탄화.

**Day 1-AM — 호스트 빌드 + 핵심 로직 first green**
- [ ] PC 호스트 CMake 프로젝트 + gtest/Catch2 골격. `computeDb` 최소 구현 → **(a) 0 dBFS(풀스케일 구형/DC 입력) / (b) -100(무음) / (c) -20 dBFS(1/10 구형/DC 입력)** 케이스 green(C1 기대값). *주의: 사인파를 넣으면 0 dBFS가 아니라 -3.01 dBFS다(RMS=진폭/√2) — C1 함정이니 구형/DC로 검증.* *(에뮬레이터/Oboe 없이 "내 C++ 코드가 돈다"를 먼저 확보 — 신규 NDK 개발자 동기/디버깅 루프 확립)*

**Day 1-PM — 환경 정상성 분리 검증 → 내 프로젝트 Oboe 링크**
- [ ] AVD 생성: API 26+ x86_64, "Virtual microphone uses host audio input" 활성화.
- [ ] **환경 정상성 분리 확인:** Oboe 공식 **hello-oboe(또는 RecordingCallback) 샘플 clone → `assembleDebug`** 성공 → "NDK/CMake/AVD/Oboe 환경 자체는 정상"을 내 프로젝트와 분리해 먼저 입증(막혔을 때 내 코드 vs 환경 切り分け).
- [ ] B방식 프로젝트 골격: Flutter 앱(or FFI 플러그인) + `android/.../cpp/CMakeLists.txt` + `externalNativeBuild`, `abiFilters 'x86_64'`, **`arguments "-DANDROID_STL=c++_shared"`** (Oboe 권장 STL; prefab/소스 모두 c++_shared 일관).
- [ ] Oboe 통합 — **1차: 소스 `add_subdirectory` (순서 역전, Synthesis 반영)** — `git submodule add` Oboe(버전 태그 고정) + `add_subdirectory(oboe)` + `target_link_libraries(ndk_audio oboe)`. *근거: 신규 개발자에게 버전이 레포에 고정되어 재현성↑, lldb로 Oboe 내부까지 스텝인 가능, 에뮬레이터 ABI 이슈 디버깅 쉬움.*
- [ ] **선택 시연: prefab AAR** — 시간이 남으면 `implementation "com.google.oboe:oboe:1.9.x"` + `buildFeatures { prefab true }` + `find_package(oboe REQUIRED CONFIG)` + `target_link_libraries(... oboe::oboe)`로 동일 빌드를 시연(README/ADR에 "두 경로 모두 검증" 어필). prefab이 막히면 무시하고 소스 유지.
- [ ] `libndk_audio.so`가 빌드되고, Oboe 심볼 링크 확인용 더미 `engine_ping()` 호출 → Logcat 출력.
- [ ] Android Studio lldb로 더미 네이티브 함수에 breakpoint 연결 성공.
- **수용 기준 (검증법):** (AM) 호스트 `ctest`에서 `computeDb` 케이스 green. (PM) hello-oboe 샘플 `assembleDebug` 성공 + 내 프로젝트 `flutter run`(또는 assembleDebug) 성공 → 에뮬레이터 Logcat에 `engine_ping` 출력 + lldb breakpoint hit 스크린샷. **이 게이트 실패 시 일정 재조정(§7).**

### Phase 1 — 네이티브 코어 (Day 2~5)

**Day 2 — Oboe 캡처**
- [ ] `AudioStreamBuilder`로 Input 스트림(I16/mono/48000/LowLatency, SharingMode 폴백 로깅).
- [ ] `onAudioReady`에서 프레임 수 + 첫 샘플값 (throttle된) Logcat 확인.
- [ ] **48000Hz Go/No-Go 단언 (M3-Day2):** 스트림 오픈 직후 `stream->getSampleRate() == 48000` 단언/로깅. 48000이 아니면 **Go/No-Go**: ① AVD 이미지/API 레벨 변경 시도 → ② 그래도 거부 시 48000 강제 정책(M3) 완화 재검토(최후 수단). M3 강제 정책상 48000 거부 시 start가 거부돼 데모가 안 켜지므로 **Day2에서 조기 탐지** 필수.
- [ ] **AAudio 폴백 확인 (Open Q4 통합):** x86_64 에뮬레이터에서 AAudio 미지원 시 Oboe가 OpenSL ES로 폴백하여 Input 캡처가 동작하는지 Logcat으로 확인(실제 채택 backend 로깅). 실패 시 API/이미지 변경 또는 실기기 1대 보조.
- **수용 기준:** 에뮬레이터에서 호스트 마이크에 소리 입력 시 비-제로 프레임 수신 Logcat 확인 + `getSampleRate()==48000` 확인 + 채택 backend(AAudio/OpenSL ES) 로깅.

**Day 3 — DbCalculator + DbWindowAccumulator (PC 호스트 테스트)**

- [ ] `computeDb(const int16_t*, count)` — stateless RMS→dBFS(기획서 §5-1, 무음 `rms<1e-7` → -100 클램프).
- [ ] **`DbWindowAccumulator` (상태 보유 클래스) — C2 신규 명세:**
  - 이유: Oboe `onAudioReady(stream, audioData, int32_t numFrames)`의 `numFrames`는 **콜백마다 가변**이며 `kWindowSamples(6000)`의 약수/배수 보장이 없다. 따라서 윈도우 경계 정렬은 별도 누산기가 책임진다.
  - 인터페이스(개념):
    - `void push(const int16_t* frames, int32_t numFrames)` — 내부 누산 버퍼에 append. 누산 길이가 `kWindowSamples` 도달할 때마다 **딱 그 6000 샘플로 `computeDb` 1회 호출 → 콜백/리스너로 dB 1회 방출**, 소비한 6000 샘플 제거 후 **잔여 샘플은 carry-over**(다음 push와 합산). 한 번의 push가 여러 윈도우를 만들면 윈도우마다 반복 방출.
    - 누산 버퍼는 **고정 크기(최대 1윈도우 + 최대 콜백 청크) 사전 할당** — 콜백 경로에서 동적 할당 금지. 증분은 **`sumSq` 러닝합(부분합 스칼라)으로 O(n) 1패스 — 필수**(샘플 버퍼 carry-over 대신 부분합만 유지하면 메모리·복사 최소화, P3 정신과 합치).
  - dB 방출은 곧 "125ms 주기 dB"의 단일 진실 소스(엔진→Dart throttle, §Day7도 이 방출 시점만 사용).
- [ ] PC 호스트 gtest:
  - (a) **풀스케일 구형/DC 상수**(±32767) 입력 → **0 dBFS(±0.3)** *(C1: 사인파는 0 dBFS가 아님 — RMS=진폭/√2)*
  - (b) 무음(전부 0) → **-100**
  - (c) **1/10 진폭 구형/DC**(±3277) 입력 → **-20 dBFS(±0.3)**
  - (선택, 사인파 검증 시) 풀스케일 사인파 → **-3.01 dBFS(±0.3)**, 1/10 사인파 → **-23.01 dBFS(±0.3)**
  - (d) `kWindowSamples == 6000`(=`kSampleRate*0.125`) 윈도우 카운트 단언.
  - (e) **가변 청크 등가성 — C3 신규:** `DbWindowAccumulator`에 `7, 192, 4096, 1, 5800, ...` 프레임을 순차 push했을 때 **(flush 횟수 · 각 윈도우 dB · 최종 carry-over 잔여 샘플 수)** 가 동일 PCM을 6000 단일 버퍼로 끊어 넣은 기준 결과와 **완전히 일치**함을 단언.
- **수용 기준:** `ctest` PC에서 (a)~(e) 전 케이스 green. 에뮬레이터 불필요. 특히 (e)로 "콜백 가변 numFrames에도 윈도우 경계·dB·carry-over 정확"이 증명됨.

**Day 4 — RingBuffer + SPSC 큐 (PC 호스트 테스트)**
- [ ] 고정크기 `RingBuffer<int16>` pre-roll = `kPrerollSamples`(=`kSampleRate*3`=144,000 샘플) wrap-around 기록. 가장 오래된 샘플을 덮어쓰는 순환(이게 pre-roll의 정상 동작).
- [ ] **소유권 (C4-R 일원화): RingBuffer는 워커 스레드가 소유**한다(= 롤링 pre-roll과 동일 객체, §Day5 참조). 콜백은 RingBuffer를 직접 만지지 않는다.
  - **데이터 흐름:** `콜백 → PcmBlock을 SPSC 큐에 push → 워커가 큐에서 꺼내 자신의 RingBuffer(pre-roll)에 wrap-around 기록`. (콜백↔워커 race 원천 제거)
- [ ] lock-free SPSC 큐(콜백→워커 핸드오프, PCM 블록 단위): `std::atomic` head/tail, acquire/release. 큐 슬롯/버퍼는 사전 할당.
  - **큐 용량 노트:** 큐 용량은 pre-roll보다 충분히 크게(예: **4초 분량 이상**의 PcmBlock 슬롯)로 잡아, 워커 소비가 잠깐 지연돼도 pre-roll(3초)에 구멍이 생기지 않게 한다.
- [ ] **SPSC overflow 정책 (drop+count 확정):** 소비자(워커) 지연으로 큐 full일 때 콜백은 **블로킹/대기 금지 → 해당 블록 drop + `std::atomic<uint64_t> droppedBlocks` 증가**. 워커는 주기적으로 dropped 카운트를 (콜백 외에서) Logcat 기록. *RingBuffer는 overwrite(정상), 큐는 drop — 둘은 다른 자료구조이므로 정책을 분리 명시.*
- [ ] PC 호스트 gtest: (1) RingBuffer wrap-around 무결성(연속 write 후 최근 N 샘플 정확) — **워커가 주입받는 형태로 테스트(소유권 무관하게 단위 테스트 그대로 유효)**, (2) 큐 정상 enqueue/dequeue FIFO 순서, (3) 큐 full 시 drop + droppedBlocks 증가 확인, (4) 단일생산자(콜백 모사)/단일소비자(워커 모사) 스레드 동시 구동 시 ThreadSanitizer race 0.
- **수용 기준:** `ctest` green + TSan 경고 0 + drop 케이스에서 카운터 정확.

**Day 5 — EventDetector + WAV 저장 워커 (스레드 분리) — C4 상태머신 확정**

설계 원칙(SPSC 일관성 + C4-R 데이터구조 일원화): **콜백은 (DbWindowAccumulator 누산 + SPSC 큐 push)만 한다.** 워커가 큐에서 받은 PcmBlock을 **자신이 소유한 RingBuffer(= 롤링 pre-roll, 동일 객체)** 에 wrap-around 기록하고, IDLE 상태에선 가장 오래된 샘플을 overwrite한다. 트리거 시 워커가 자기 RingBuffer에서 직전 `kPrerollSamples`를 스냅샷해 WAV 앞부분으로 확정한다. **콜백은 RingBuffer를 직접 만지지 않는다(race 원천 제거).** → pre-roll PCM은 단 한 곳(워커 소유 RingBuffer)에만 보관되며, 이중 보관/dead store가 없다.

**큐로 흐르는 메시지 타입 (단일 SPSC, 콜백→워커):**
- `PcmBlock { int16 data[N]; }` — 매 콜백 PCM (항상 흐름)
- `TriggerEvent { uint64 timestampSample; }` — 임계 초과 순간 1회 마커
- `StopFlush {}` — stop 시 잔여 처리/종료 신호

**워커 측 이벤트 상태머신 (단일 소비자):**
- 워커는 들어오는 `PcmBlock`을 **자신이 소유한 RingBuffer(= 롤링 pre-roll 버퍼, `kPrerollSamples` 크기, 단일 객체)** 에 항상 wrap-around 기록한다. *용어 통일: 본 문서의 "RingBuffer"와 "롤링 pre-roll 버퍼"는 동일한 하나의 객체를 가리킨다(별개 버퍼 아님).* 이 객체는 워커만 접근하므로 콜백↔워커 race가 없다.
- 상태: `IDLE` / `CAPTURING`.
  - `IDLE`에서 `TriggerEvent` 수신 → `CAPTURING` 진입. 현재 롤링 pre-roll(직전 `kPrerollSamples`) 스냅샷을 새 WAV의 앞부분으로 확정 + `postrollRemaining = kPostrollSamples` 설정 + 새 WAV 파일 핸들 open.
  - `CAPTURING` 중 매 `PcmBlock` → WAV에 append + `postrollRemaining -= block.numSamples`. `postrollRemaining <= 0` 이면 WAV finalize(헤더 기록) + close → `IDLE`.
- **연속/중첩 이벤트 정책 (명문화):** `CAPTURING` 중 추가 `TriggerEvent` 수신 시 **새 WAV를 만들지 않고 `postrollRemaining = kPostrollSamples`로 리셋(연장/merge)**. 즉 끊임없이 시끄러우면 하나의 긴 WAV로 이어 붙고, 마지막 트리거 후 3초가 지나야 종료. (분리 저장이 아니라 병합 — 데모에서 파일 폭증/경계 모호 방지)
- **stop 시 in-flight 처리:** Dart `engine_stop()` → 콜백 측은 스트림 정지 전에 `StopFlush` 큐잉. 워커는 `StopFlush` 수신 시 `CAPTURING`이면 **현재까지 수집분으로 WAV finalize 후 close**(post-roll 미완이어도 잘라서 저장) → 워커 루프 종료. 메인은 워커 `join()` 후 리소스 해제(순서: 스트림 stop/close → 큐에 StopFlush → 워커 join → finalize 완료).
- [ ] EventDetector: `computeDb`(DbWindowAccumulator) 방출값 > threshold → 콜백이 `TriggerEvent`를 SPSC 큐에 push(비블로킹, 콜백 경로). 트리거 판정도 콜백의 2개 책임(누산+큐push) 안에 포함 — 별도 무거운 작업 없음.
- [ ] 저장은 워커 스레드에서만 file I/O(WAV 16bit PCM/48k/mono). 콜백은 절대 파일 안 만짐.
- [ ] (호스트 gtest 가능 범위) 상태머신을 PCM 소스/파일 I/O에서 분리(인터페이스 주입)하여 **트리거 1회/연속 2회 병합/stop in-flight** 시 (생성 WAV 개수, 각 WAV 샘플 수, postrollRemaining 전이)를 단언.
- **수용 기준:**
  - 에뮬레이터에서 박수/소음 1회 → 저장 경로(`getExternalFilesDir`)에 WAV 1개 생성. `adb pull` 후 재생 시 트리거 전후 구간 들림.
  - **연속 2회 트리거(3초 이내 재트리거)** → WAV가 **1개로 병합**되고 길이가 마지막 트리거 기준 +3초까지 연장됨(분리 저장 안 됨)을 확인.
  - 호스트 gtest 상태머신 케이스 green.

### Phase 2 — FFI 브릿지 (Day 6~7)

**Day 6 — 제어 함수 export + 권한**
- [ ] C++ export(`extern "C"`): `engine_start()`, `engine_stop()`, `engine_set_threshold(float)`, **`engine_set_db_callback(void (*cb)(float dbValue))`** *(기획서 §5-4엔 있으나 이전 초안 누락 — 보완)*. 콜백 포인터는 `engine_start` 이전에 등록, `engine_stop` 이후 해제.
- [ ] Dart: `DynamicLibrary.open('libndk_audio.so')` + typedef/lookup + `NativeFinalizer` 리소스 해제. dB 콜백은 `NativeCallable.listener`로 만든 함수 포인터를 `engine_set_db_callback`에 전달.
- [ ] **RECORD_AUDIO 런타임 권한**: Dart에서 요청(권장: `permission_handler` 패키지) → granted 후에만 `engine_start()` 호출. AndroidManifest에 `<uses-permission android:name="android.permission.RECORD_AUDIO"/>`. 거부/영구거부 분기 UI 안내.
- [ ] (선택) A안 스파이크 1~2h: `package_ffi`로 동일 .so 빌드 가능한지 탐색. 빠르면 채택, 아니면 B 유지.
- **수용 기준:** Flutter 버튼으로 start/stop/threshold 변경이 네이티브에 반영(Logcat). `engine_set_db_callback` 등록 후 콜백 함수가 호출됨(Day7에서 값 검증). 권한 거부 시 start 미호출 + 안내 표시. 권한 허용 후 캡처 시작.

**Day 7 — dB 실시간 콜백**
- [ ] engine→Dart dB 콜백: `NativeCallable.listener`로 isolate 안전 전달. 네이티브는 **125ms마다 1회만** 콜백 호출(throttle 네이티브 측 보장).
- [ ] **N2 — P3 무충돌 확인:** `NativeCallable.listener`는 네이티브가 콜백을 호출하면 **타깃 isolate의 이벤트 루프에 메시지로 전달**되어 isolate-safe하다(콜백 호출 자체는 비블로킹 enqueue). 단, dB 방출 시점이 오디오 콜백 스레드라도 **콜백 안에서 로그/lock/alloc 금지(P3)** 는 그대로 유지 — listener 호출은 가벼운 enqueue만이므로 P3와 무충돌. (직접 메모리 공유/동기 Dart 호출 회피)
- [ ] Dart 측: 콜백 → `StreamController` → UI Stream.
- **수용 기준:** Flutter에 약 8Hz로 dB 값 도달(로그로 주기 확인). 장시간에도 isolate 크래시 없음.

### Phase 3 — UI + 안정성 (Day 8~9)

**Day 8 — Flutter UI**
- [ ] dB 게이지(StreamBuilder), 임계값 슬라이더(→`engine_set_threshold`), 이벤트 로그 리스트.
- **수용 기준:** 소리 크기에 따라 게이지 실시간 변동, 슬라이더 조정 시 이벤트 트리거 임계 변화 체감.

**Day 9 — WAV 재생 UI + 안정성**
- [ ] 저장 WAV 목록/재생(`just_audio` 등) UI. 목록은 `getExternalFilesDir`의 `events/` 디렉터리 스캔.
- [ ] **30분 연속 측정 + 반복 트리거 안정성 (M2):** 단순 idle 측정이 아니라 **1~2분마다 의도적으로 임계 초과(박수/소음)를 반복**하여 EventDetector→SPSC 큐→워커→WAV 저장 경로를 반복 가동(이벤트 누적 약 15~30건). 워커 스레드/파일 핸들/큐 메모리의 반복 사용 누수를 노출시키는 것이 목적.
- [ ] **메모리 측정(절대값 비교):** 세션 **시작 직후**와 **30분 종료 직전** `adb shell dumpsys meminfo <pkg>`의 **Native Heap(절대값)** 캡처 → **증가량 < 5MB** 를 통과 기준으로. Android Studio Profiler 메모리 그래프(우상향 추세 여부) 병행 + 스크린샷 보관.
- [ ] **AddressSanitizer:** ASan 디버그 빌드로 **반복 트리거를 포함한 5~10분 세션** 1회 실행 → ASan 리포트 0(heap-use-after-free/leak 없음). (idle만이 아니라 저장 경로가 도는 동안 실행)
- **수용 기준:** 30분 반복 트리거 세션 종료 후 크래시 0 + Native Heap 증가 < 5MB(시작/종료 dumpsys 캡처) + Profiler 그래프 누수 추세 없음(스크린샷). ASan 리포트 0. WAV 재생 정상.

### Phase 4 — 문서/공개 (Day 10)

- [ ] README **30초 검증 4포인트** 위치 명시: (1) Oboe 스트림 설정 코드, (2) `computeDb` dB 연산 코드, (3) FFI 브릿지(NativeCallable) 코드, (4) 콜백/워커 스레드 분리 구조.
- [ ] README **재현 단계에 submodule 클론 명시**: Oboe가 git submodule이므로 `git clone --recursive` (또는 clone 후 `git submodule update --init`) 안내 — 누락 시 제3자 clone 빌드 실패(`add_subdirectory(oboe)` 경로 없음).
- [ ] **환경 한계 명시**: 에뮬레이터 호스트 마이크 경로의 OS 처리/AGC 가능성, "유사 SPL(dBFS+오프셋)" 한계, native-assets vs Gradle+CMake 선택 사유, 실기기/외장 마이크는 클라이언트 장비로 진행 예정.
- [ ] GitHub 공개(소스 + README + CMakeLists) + 시연 영상/스크린샷.
- **수용 기준:** 처음 보는 사람이 README로 30초 안에 4포인트 코드 위치를 찾을 수 있음. 레포 clone 후 빌드 단계가 문서대로 재현 가능.

> 분리(개발 외 — 본 계획 비포함): Notion 프로젝트 DB 업로드, 위시켓 지원서 재작성. 별도 트랙으로 처리.

---

## 6. Acceptance Criteria — 기획서 8장 체크리스트의 검증 가능 재정의

| # | 완료 기준 | 검증 방법(테스트 가능 형태) |
|---|---|---|
| AC1 | NDK C++ 빌드 성공 (CMake + Oboe 링크) | `flutter run`/`assembleDebug` exit 0 + `libndk_audio.so`에 Oboe 심볼 링크. `engine_ping` Logcat 출력. |
| AC2 | Oboe Input 마이크 Raw Audio 캡처 | 에뮬레이터 호스트 마이크 입력 시 `onAudioReady` 비-제로 프레임 수신 Logcat 확인. |
| AC3 | 125ms 주기 RMS→dB 연산 (가변 청크 정확) | PC 호스트 gtest: **0 dBFS(풀스케일 구형/DC, ±0.3) / -100(무음) / -20 dBFS(1/10 구형, ±0.3)** + 윈도우카운트(6000) + **가변청크 등가성(7·192·4096·1·5800 → flush 횟수·dB·carry-over가 6000 단일버퍼 기준과 일치)** 전부 green(`ctest`). *(사인파 검증 시 -3.01/-23.01 dBFS)* |
| AC4 | 순환 버퍼 전후 ~6초 WAV 저장 (측정 가능) | 호스트 gtest(RingBuffer wrap-around / 큐 FIFO / 큐 full drop+카운트 / TSan 0 / 상태머신) + 에뮬레이터 실파일: **WAV 헤더 샘플 수 = 288000±4800(6초±0.1초 @48k)** 확인(육안 아님). **연속 2회 트리거 시 1개 WAV로 병합**되고 길이가 마지막 트리거 +3초까지 연장됨 확인. **stop in-flight 시** 잘린 WAV가 정상 finalize됨 확인. |
| AC5 | Dart FFI dB 콜백 + 엔진 제어 | UI start/stop/threshold가 네이티브 반영(Logcat) + `engine_set_db_callback` 등록 + Dart에 ~8Hz dB 도달(주기 로그). |
| AC6 | Flutter UI(게이지+임계+로그+재생) | 수동 시연: 게이지 실시간 변동, 슬라이더 동작, 이벤트 로그 적재, 저장 WAV 재생. |
| AC7 | 30분 연속 + 반복 트리거 크래시/누수 없음 | 30분 세션 중 **1~2분마다 임계 초과 반복 트리거**(WAV 경로 반복 가동) → 크래시 0 + **`dumpsys meminfo` Native Heap 시작/종료 절대값 증가 < 5MB** + Profiler 그래프 누수 추세 없음(스크린샷). (보조) ASan **반복 트리거 포함** 5~10분 세션 리포트 0. |
| AC8 | GitHub 공개 + README 30초 검증 | 레포 공개 + README 4포인트 코드 위치 + 한계 명시. 제3자가 30초 내 4포인트 도달 가능. |

> 개발 외 항목(별도): Notion 업로드, 위시켓 재지원 — 본 수용 기준에서 제외.

---

## 7. 구체적 검증 단계 (빌드/실행/안정성/누수)

1. **빌드 검증:** `flutter run --debug` (또는 `./gradlew assembleDebug`) → 에뮬레이터 설치 실행. 실패 시 Gradle/CMake 로그에서 Oboe `find_package`/링크 단계 확인.
2. **캡처 검증:** 에뮬레이터 마이크 ON, 호스트에서 음성/박수 → Logcat에서 프레임 수신 + dB 값 변동 관찰.
3. **단위 검증(빠른 루프):** PC에서 CMake 호스트 빌드 → `ctest`로 DbCalculator/RingBuffer/SPSC 큐. CI 없이도 로컬 초 단위 피드백.
4. **저장 검증:** 임계값 낮게 설정 → 트리거 → `adb pull <getExternalFilesDir>/events/*.wav` → WAV 헤더의 데이터 청크 샘플 수 파싱하여 **288000±4800(6초±0.1초)** 확인 + 호스트 플레이어로 전후 구간 청취. 연속 2회 트리거로 병합(1파일/연장) 동작 확인.
5. **FFI 검증:** Dart에서 콜백 수신 타임스탬프 로깅 → 인접 콜백 간격 ≈ 125ms 확인(throttle 검증).
6. **30분 안정성(반복 트리거):** 에뮬레이터에서 30분 연속 측정하며 **1~2분마다 의도적 임계 초과 트리거**. 시작/종료 시 `adb shell dumpsys meminfo <pkg>` Native Heap 절대값 캡처 → 증가 < 5MB. Android Studio Profiler 메모리 모니터링(누수 추세) + 종료 후 크래시 로그 0. 스크린샷 보관.
7. **누수 보조:** AddressSanitizer 디버그 빌드로 **반복 트리거 포함** 5~10분 세션 1회 → ASan 리포트 0. (TSan은 호스트 테스트에서 상시.)

---

## 8. 일정 (기획서 Day 1~10 검토 + 리스크 반영 조정)

기획서 일정은 합리적이나 **두 가지 빌드 통합 리스크를 끝(Day 6)에 두면 마감 위험**이 크다. 조정 핵심: ① 빌드 통합 게이트를 Day 1로 전진, ② 순수 로직을 PC 호스트 테스트로 당겨 피드백 단축, ③ native-assets(A)는 스파이크로만.

| Day | 조정된 작업 | 비고(기획서 대비 변경) |
|---|---|---|
| 1 | **빌드 통합 게이트(B, AM/PM 분할):** AM=호스트 gtest `computeDb` green / PM=hello-oboe 샘플 환경검증 → 내 프로젝트 소스 add_subdirectory Oboe 링크 + engine_ping + lldb | 기획서 Day1 + **Oboe 링크/lldb를 Day1로 전진(리스크 게이트) + AM/PM 분할(Synthesis)** |
| 2 | Oboe Input 스트림 + 콜백 프레임 수신 Logcat | 기획서 Day2 유지 |
| 3 | DbCalculator + **PC 호스트 gtest** | 호스트 테스트 명시 |
| 4 | RingBuffer + **SPSC 큐** + PC 호스트 gtest(+TSan) | SPSC 큐를 Day4로 명시(기획서엔 암묵) |
| 5 | EventDetector + WAV 저장 워커(pre/post-roll 핸드오프) | post-roll 라이브 캡처 구조 명세 |
| 6 | FFI 제어 export + **RECORD_AUDIO 권한** + (선택) A 스파이크 | **권한 처리 추가(기획서 누락)** |
| 7 | NativeCallable.listener dB 콜백(125ms throttle) → Dart Stream | throttle 네이티브 보장 명시 |
| 8 | Flutter UI: 게이지/슬라이더/이벤트로그 | 기획서 Day8 유지 |
| 9 | WAV 재생 UI + 30분 안정성 + 메모리/ASan 점검 | 기획서 Day9 유지 |
| 10 | README(4포인트+한계) + GitHub 공개 + 시연 | 기획서 Day10 유지 |

- **버퍼 전략:** Day 1 게이트 실패(Oboe 통합 난항) 시 → prefab↔소스 폴백 즉시 전환에 최대 +1일. 그래도 안 되면 Day 8~9 UI를 최소화(게이지+로그만, WAV 재생 후순위)하여 마감 우선(기획서 §10 일정 초과 대응과 동일 원칙).
- **A안 스파이크 타임박스:** Day 6에서 최대 2시간. 초과 시 즉시 B 확정.

---

## 9. ADR (Architecture Decision Record) — 확정 (iteration 3)

> 상태: **확정** (Architect/Critic review 반영). 단, "A 스파이크 채택 여부"와 "prefab 시연 성공 여부"는 실행 중 결과를 본 항목에 append.

- **Decision (확정):**
  1. Android 네이티브 빌드 = **전통 Gradle + CMake(externalNativeBuild)**(옵션 B). `-DANDROID_STL=c++_shared`, `abiFilters 'x86_64'`.
  2. Oboe 통합 = **소스 `add_subdirectory(oboe)`를 primary**(submodule 버전 고정). **prefab AAR은 시간 여유 시 "두 경로 모두 검증" 시연용**(순서 역전 — 기존 prefab-primary에서 변경).
  3. native-assets(옵션 A) = Day 6 **최대 2시간 타임박스 스파이크**. 빠르게 동작 시에만 채택, 아니면 B 유지(마감 비의존).
  4. 샘플레이트 = **48000Hz 강제**, 불일치 시 에러+start 거부(폴백 리샘플 안 함). 상수는 `kSampleRate` 파생.
  5. SPSC 큐 overflow = **drop + `droppedBlocks` 카운트**(RingBuffer는 overwrite로 분리).
  6. post-roll = **워커 측 상태머신(IDLE/CAPTURING)**, 연속 트리거 시 **병합(연장)**, stop 시 in-flight **finalize 후 join**. **데이터구조 일원화(C4-R): RingBuffer(=롤링 pre-roll, 단일 객체)는 워커가 소유하고, 콜백은 SPSC 큐 push만 한다**(콜백은 RingBuffer 미접근 → race 제거 + 콜백 경량화로 P3 강화).
- **Drivers:** ① 마감 리스크(D-14, 신규 C++) ② 채용 검증성(읽기 쉬운 표준 NDK) ③ 디버깅(lldb 1급 지원).
- **Alternatives considered:**
  - A(native-assets/build hooks): 2026-05 stable이나 Oboe(prefab) 연동이 native-assets 정상 경로 아님 + 신규 개발자 탈출구 좁음 → 스파이크로 강등.
  - Oboe prefab-primary: 모던하나 신규 개발자에겐 버전/ABI 디버깅이 어렵고 lldb 스텝인 제약 → 소스-primary로 역전(Driver ③ 일관).
- **Why chosen:** Oboe가 전통 Gradle+CMake에 최적화. 소스 add_subdirectory가 버전 재현성·lldb 디버깅·에뮬레이터 ABI 切り分け에서 신규 개발자에게 가장 결정적. 디버깅·예제·마감 리스크 모두 B+소스 우위.
- **Consequences:** 빌드파일 수기 작성(보일러플레이트), "구식" 인상 → README에서 의도적 선택임 + "prefab/소스/native-assets 비교 검증"으로 상쇄. 순수 Dart FFI 패키지 아님. submodule 관리 필요.
- **Follow-ups (실행 중 append):** (1) A 스파이크 결과(채택/기각 + 사유), (2) prefab 시연 성공 여부, (3) 에뮬레이터 마이크 경로 한계를 README에 영구 기록.

---

## 10. Open Questions (iteration 3 — C4 데이터구조 해소)

1. **[검증 필요]** `native_toolchain_c`/build hooks가 Oboe(소스/prefab)를 Android에서 링크할 수 있는가? → A안 채택 여부. **완화:** ADR에서 A를 스파이크로 강등 + 소스 add_subdirectory(B)를 primary로 확정했으므로 마감 비의존. 스파이크 결과만 ADR Follow-up에 기록.
2. **[검증 필요]** NDK r28 + Oboe 1.9.x **소스 빌드**의 x86_64 ABI 호환(에뮬레이터). 실패 시: prefab AAR 시연 경로 또는 ABI 옵션 조정. *(primary가 소스로 바뀌어 prefab ABI 불일치 리스크는 1차 경로에서 제거됨)*
3. **[검증 필요]** 에뮬레이터 호스트 마이크 경로에 Windows/에뮬레이터 단의 AGC/노이즈 처리가 끼는지. → "깨끗한 입력" 가정이 틀릴 수 있음 → README 한계로 명시(데모 합격엔 무관).
4. **[검증 필요 — 신규]** x86_64 **에뮬레이터에서 AAudio 미지원** 시 Oboe가 OpenSL ES로 정상 폴백하여 Input 캡처가 동작하는가? + **48000Hz Input 수용 여부**(M3 강제 정책 의존). 실패 시: API 레벨/이미지 변경 → 그래도 거부 시 48000 강제 완화 재검토(최후). **Day 2 캡처 검증에서 Go/No-Go로 조기 탐지**(§Day2, M3-Day2).
5. **[확인]** Flutter 3.44에서 `package_ffi` 템플릿/build hooks 실동작 버전 핀(3.38+ stable이나 3.44 구체 동작 확인 권장). *(A 스파이크 시에만 영향)*
6. **[해소]** SPSC 큐 overflow 정책 → **drop + droppedBlocks 카운트로 확정**(§Day4, ADR 5). RingBuffer는 overwrite로 분리.
7. **[해소 — iteration 3]** post-roll 핸드오프/pre-roll 데이터구조 → **RingBuffer = 롤링 pre-roll = 워커 소유 단일 객체**로 일원화(이중 보관/dead store 제거). 콜백은 SPSC 큐 push만, 워커가 큐 소비→RingBuffer write→트리거 시 스냅샷. 상태머신(IDLE/CAPTURING, 병합, stop finalize) 확정(§Day5, ADR 6, C4-R).
8. **[해소]** dBFS 기대값 산술 → 구형/DC로 0/-20 dBFS 유지(사인파는 -3.01/-23.01)로 확정(§Day3, C1).
