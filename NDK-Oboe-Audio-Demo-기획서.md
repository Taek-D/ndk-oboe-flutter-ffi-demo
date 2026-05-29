# NDK-Oboe 오디오 캡처 + Flutter Dart FFI 데모 — 기획서

> 작성일: 2026-05-29
> 목적: 위시켓 「기존 소스 활용 층간소음 측정 앱 NDK 고도화 및 모니터링 웹 개발」 공고 지원 전, NDK C/C++ 오디오 프로세싱 직접 구현 경험을 증명하는 포트폴리오 확보

---

## 1. 연관 공고

| 항목 | 내용 |
|---|---|
| 공고명 | 기존 소스 활용 층간소음 측정 앱 NDK 고도화 및 모니터링 웹 개발 |
| 예산 / 기간 | 20,000,000원 / 70일 |
| 모집 마감일 | 2026-06-12 (D-14 기준 2026-05-29) |
| 채우려는 부족 역량 | Android NDK C/C++ Raw Audio 캡처, 0.125초 주기 dB 연산, 순환 버퍼, Flutter ↔ NDK Dart FFI 브릿지 |
| 지원 자격(필수) | "Android NDK를 활용한 오디오 프로세싱 개발 경험 필수" |

이 데모의 목적은 **층간소음을 정확히 측정하는 것**이 아니라, **NDK C/C++ 오디오 코드를 작성하고 Flutter와 FFI로 연결할 수 있다는 사실을 동작하는 코드로 증명하는 것**이다. AGC 우회 정확도 검증은 계약 후 클라이언트 제공 장비(공기계 + 외장 마이크 키트)로 진행하는 영역이므로 본 데모 범위에서 제외한다.

---

## 2. 목표 / 비범위

### 2-1. 달성 목표 (Definition of Done)

- 에뮬레이터에서 NDK C++ 코드가 빌드·실행되고, PC 마이크 입력을 Raw Audio로 캡처한다.
- C++ 레벨에서 0.125초(125ms) 주기로 RMS 기반 데시벨을 연산한다.
- 순환 버퍼로 임계값 초과 이벤트 발생 시 전후 3초 구간을 WAV로 로컬 저장한다.
- Dart FFI로 실시간 dB 값을 Flutter UI에 전달하여 게이지로 표시한다.
- GitHub 공개 + README에 "NDK r28 + Oboe + Dart FFI" 아키텍처를 30초 안에 검증 가능하도록 문서화한다.

### 2-2. 비범위 (이번 데모에서 하지 않는 것)

- 실기기 기종별 AGC(자동 이득 제어) 우회 검증 → 계약 후 클라이언트 장비
- 외장 마이크(C타입/오디오 잭) 연동 → 계약 후 클라이언트 장비
- 외장 마이크 시리얼별 캘리브레이션 파일 동기화 → 본 프로젝트 본계약 영역
- 실제 dB SPL 절대값 정확도 보정 → 캘리브레이션 데이터 필요(클라이언트 제공)
- Firebase 연동, 모니터링 웹 → 사이드 프로젝트 2번에서 별도 진행

---

## 3. 기술 스택 (2026-05 기준 최신 검증 완료)

| 영역 | 기술 | 버전 | 비고 |
|---|---|---|---|
| 네이티브 오디오 | Oboe | 1.9.x | Google 공식 AAudio C++ 래퍼. OpenSL ES는 deprecated이므로 사용하지 않음 |
| NDK | Android NDK | r28 | 16KB 페이지 대응, AAudio 지원 |
| 빌드 | CMake | 3.22+ | NDK 네이티브 빌드 |
| 언어(네이티브) | C++17 | — | RMS/dB 연산, 순환 버퍼 |
| 크로스플랫폼 | Flutter | 3.44 | 최신 stable |
| FFI | Dart FFI | dart:ffi | Flutter 3.38+ 권장 방식(package_ffi 템플릿 + build hooks) |
| 최소 SDK | Android API 26 (8.0) | — | AAudio 최소 지원 버전 |
| 개발 환경 | Android Emulator | API 26+, x86_64 | AVD에서 마이크 입력 활성화 |

> 검증 메모: OpenSL ES는 2026년 현재 공식 deprecated 상태이며 Android 개발자 문서가 Oboe 사용을 권장한다. Oboe는 AAudio가 가능하면 AAudio를, 아니면 OpenSL ES로 자동 폴백한다. Flutter-NDK 연동은 Flutter 3.38부터 `flutter create --template=package_ffi`가 권장 방식이다.

---

## 4. 아키텍처 설계

```
[마이크 입력]  (에뮬레이터는 호스트 PC 마이크를 사용 → AGC 없는 깨끗한 입력)
      │
      ▼  Oboe Input Stream (오디오 콜백 스레드, 실시간 우선순위)
┌─────────────────────────────────────────────┐
│ C++ Audio Engine (libndk_audio.so)           │
│   ├─ RingBuffer<int16>   최근 3초 PCM 보관    │
│   ├─ DbCalculator        125ms 윈도우 RMS→dB │
│   └─ EventDetector       임계값 초과 → 저장   │
└──────────────┬──────────────────────────────┘
               │  Dart FFI
               │   · NativeCallable로 dB 값 콜백 (engine → dart)
               │   · start()/stop()/setThreshold() 함수 export (dart → engine)
               ▼
┌─────────────────────────────────────────────┐
│ Dart FFI Bridge (audio_engine.dart)          │
│   · DynamicLibrary.open('libndk_audio.so')   │
│   · NativeFinalizer로 리소스 해제            │
└──────────────┬──────────────────────────────┘
               ▼
┌─────────────────────────────────────────────┐
│ Flutter UI                                   │
│   · 실시간 dB 게이지 (StreamBuilder)         │
│   · 임계값 슬라이더                          │
│   · 이벤트 로그 + 저장된 WAV 목록/재생       │
└─────────────────────────────────────────────┘
```

설계 원칙: **오디오 콜백 스레드에서는 절대 블로킹 작업(파일 I/O, 로그, malloc)을 하지 않는다.** 콜백은 순환 버퍼에 쓰고 dB만 계산해 lock-free 큐로 넘기고, 파일 저장은 별도 워커 스레드에서 처리한다. (이 분리가 장시간 측정 시 크래시/글리치를 막는 핵심)

---

## 5. 핵심 구현 명세

### 5-1. dB 연산 (DbCalculator)

- 샘플레이트 48,000Hz 기준, 0.125초 윈도우 = **6,000 샘플**
- int16 PCM → float 정규화(`/ 32768.0f`) → RMS = √(mean(x²))
- dBFS = `20 · log10(rms)` (무음 보호: rms가 0이면 -100dB로 클램프)
- 실제 dB SPL 절대값은 캘리브레이션 오프셋(클라이언트 제공)이 더해져야 하므로, 데모에서는 dBFS + 임의 기준 오프셋(예: +94dB)으로 "유사 SPL" 표기하고 README에 한계 명시

```cpp
float computeDb(const int16_t* frames, int32_t count) {
    double sumSq = 0.0;
    for (int32_t i = 0; i < count; ++i) {
        const float s = frames[i] / 32768.0f;
        sumSq += static_cast<double>(s) * s;
    }
    const double rms = std::sqrt(sumSq / count);
    if (rms < 1e-7) return -100.0f;          // 무음 클램프
    return 20.0f * std::log10(static_cast<float>(rms));
}
```

### 5-2. 순환 버퍼 (RingBuffer)

- 고정 크기 링버퍼에 PCM을 연속 기록 (최근 3초 = 48,000 × 3 = 144,000 샘플)
- 임계값 초과 감지 시: 버퍼에 남아있는 **이전 3초** + 이후 **3초**를 합쳐 WAV로 저장
- 단일 생산자(오디오 콜백) / 단일 소비자(저장 워커) 구조로 lock 최소화

### 5-3. Oboe 입력 스트림

- `AudioStreamBuilder`로 Input 스트림 구성: `PerformanceMode::LowLatency`, `SharingMode::Exclusive` 우선, 실패 시 폴백
- `setFormat(AudioFormat::I16)`, `setChannelCount(1)`, `setSampleRate(48000)`
- `AudioStreamDataCallback::onAudioReady`에서 dB 계산 + 링버퍼 쓰기만 수행

### 5-4. Dart FFI 브릿지

- `flutter create --template=package_ffi`로 프로젝트 생성
- C++에서 export: `engine_start()`, `engine_stop()`, `engine_set_threshold(float)`, `engine_set_db_callback(...)`
- engine → Dart 방향 dB 콜백은 `NativeCallable.listener` 사용 (오디오 스레드에서 안전하게 Dart isolate로 전달)
- `DynamicLibrary.open()` + `NativeFinalizer`로 네이티브 리소스 누수 방지

---

## 6. 예상 일정 (9~10일)

| Day | 작업 내용 | 산출물 |
|---|---|---|
| 1 | 환경 세팅: NDK r28, Oboe 의존성, CMake, AVD(API 26 x86_64, 마이크 ON) | 빌드 성공하는 빈 NDK 프로젝트 |
| 2 | Oboe Input 스트림 구성, 콜백에서 프레임 수신 확인 (Logcat 출력) | 마이크 입력 수신 로그 |
| 3 | DbCalculator 구현 + 단위 테스트(440Hz 톤, 무음 케이스) | dB 연산 모듈 + 테스트 통과 |
| 4 | RingBuffer 구현 + 단위 테스트(wrap-around, overflow) | 순환 버퍼 모듈 + 테스트 |
| 5 | EventDetector + WAV 저장 워커 스레드 (스레드 분리) | 임계값 초과 시 WAV 저장 동작 |
| 6 | Dart FFI 브릿지: start/stop/setThreshold export | Flutter에서 엔진 제어 가능 |
| 7 | NativeCallable로 dB 실시간 콜백 → Dart Stream 연결 | Flutter에 dB 값 도달 |
| 8 | Flutter UI: dB 게이지, 임계값 슬라이더, 이벤트 로그 | 동작하는 UI |
| 9 | 저장 WAV 목록/재생 UI, 장시간(30분+) 측정 안정성 점검 | 메모리/크래시 점검 결과 |
| 10 | README 작성(30초 검증 포인트), GitHub 공개, 시연 영상/스크린샷 | 공개 레포 + 문서 |

> 버퍼: Day 9에 장시간 측정 안정성 점검 일정을 별도 배정 — NDK 네이티브 크래시(race condition, 메모리)는 이 단계에서 집중적으로 잡는다.

---

## 7. 개발 환경 (에뮬레이터)

- AVD Manager에서 **API 26 이상 / x86_64** 이미지 생성 (AAudio 지원 + 빠른 속도)
- AVD 설정에서 **마이크 입력 활성화** (Virtual microphone uses host audio input)
- 에뮬레이터는 호스트 PC 마이크를 그대로 사용 → AGC가 없는 깨끗한 입력이라 Raw Audio 처리 검증에 유리
- 디버깅: Android Studio에서 lldb가 바로 연결되며, segfault 시 네이티브 스택 트레이스 확인
- README에 "에뮬레이터 환경 개발, 실기기/외장 마이크 검증은 클라이언트 제공 장비로 진행 예정" 명시

---

## 8. 완료 기준 체크리스트

- [ ] NDK C++ 빌드 성공 (CMake + Oboe 링크)
- [ ] Oboe Input 스트림으로 마이크 Raw Audio 캡처 동작
- [ ] 0.125초 주기 RMS → dB 연산 (단위 테스트 통과)
- [ ] 순환 버퍼 전후 3초 WAV 저장 (단위 테스트 통과)
- [ ] Dart FFI로 dB 실시간 콜백 + 엔진 제어(start/stop/threshold)
- [ ] Flutter UI: dB 게이지 + 임계값 + 이벤트 로그 + WAV 재생
- [ ] 30분 연속 측정 시 크래시/메모리 누수 없음
- [ ] GitHub 공개 + README 30초 검증 포인트 문서화
- [ ] Notion 프로젝트 DB 업로드
- [ ] 위시켓 지원서 재작성 요청

---

## 9. 공고 연관성 매핑

| 공고 요구사항 | 본 데모에서 증명되는 부분 |
|---|---|
| NDK Raw Audio 수집 + 0.125초 주기 dB 연산 (C/C++ 신규 개발) | DbCalculator + Oboe 콜백으로 직접 구현 ✅ |
| 순환 버퍼로 기준 초과 전후 N초 원음 로컬 저장 | RingBuffer + EventDetector + WAV 저장 ✅ |
| NDK 오디오 코어를 Flutter 앱에 브릿지로 연동·통합 | Dart FFI 브릿지(NativeCallable) ✅ |
| 장시간 상시 측정 시 메모리 누수/크래시 방지 최적화 | 오디오 콜백/저장 스레드 분리 + Day 9 안정성 점검 ✅ |
| 가속도/자이로 센서 이동 감지 | (본 데모 범위 외 — Flutter Geofence 데모로 OS 센서 경험 별도 보유) |

---

## 10. 리스크 및 대응

| 리스크 | 대응 |
|---|---|
| C++ 문법 적응 지연 | Python + Unity C# 경험 기반. AI 보조로 보일러플레이트 생성, 핵심 로직만 직접 작성 |
| NDK 네이티브 크래시 디버깅 시간 | lldb 사용법 Day 1에 미리 익힘. 오디오 스레드에서 블로킹 작업 원천 차단 설계 |
| Dart FFI 콜백 스레드 안전성 | NativeCallable.listener 사용으로 isolate 안전 전달 (직접 메모리 공유 회피) |
| 에뮬레이터 오디오 latency | 포트폴리오 목적상 latency는 평가 대상 아님. "동작 증명"이 목표 |
| 일정 초과 | Day 1~5(네이티브)와 Day 6~10(브릿지+UI) 단계 분리. 네이티브가 늦어지면 UI를 최소화하여 마감 우선 |

---

## 11. 산출물

- GitHub 공개 레포지토리 (소스 + README + CMakeLists)
- Flutter 데모 APK 또는 시연 영상/스크린샷
- README: 30초 검증 4포인트 (Oboe 설정 위치 / dB 연산 코드 / FFI 브릿지 / 스레드 분리 구조)
- Notion 프로젝트 DB 업로드용 정리 (Problem / Solution / Challenge / Learning)
