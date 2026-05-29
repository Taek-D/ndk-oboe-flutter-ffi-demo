# Open Questions

## NDK-Oboe FFI Demo - 2026-05-29 (iteration 2 — Architect/Critic 반영)
- [ ] native_toolchain_c/build hooks가 Oboe를 Android에서 링크 가능한가? — A안. 완화: A를 스파이크로 강등, 소스 add_subdirectory(B) primary 확정 → 마감 비의존. 결과만 ADR Follow-up 기록.
- [ ] NDK r28 + Oboe 1.9.x 소스 빌드의 x86_64 ABI 호환(에뮬레이터)? — primary가 소스로 바뀌어 prefab ABI 리스크는 1차 경로에서 제거.
- [ ] 에뮬레이터 호스트 마이크 경로에 Windows/에뮬레이터 단 AGC/노이즈 처리가 끼는지? — "깨끗한 입력" 가정이 틀릴 수 있음. README 한계로 명시(데모 합격엔 무관).
- [ ] [신규] x86_64 에뮬레이터에서 AAudio 미지원 시 Oboe가 OpenSL ES로 정상 폴백하여 Input 캡처되는가? — Day2 캡처 검증에서 확인. 실패 시 API/이미지 변경 또는 실기기 1대 보조.
- [ ] Flutter 3.44에서 package_ffi 템플릿/build hooks 실동작 버전 핀 확인(A 스파이크 시에만 영향).
- [x] SPSC 큐 overflow 정책 → drop + droppedBlocks 카운트로 확정(RingBuffer는 overwrite로 분리).
- [x] post-roll 라이브 핸드오프 SPSC 충돌 → 워커가 큐만 소비 + 자체 롤링 pre-roll + 상태머신(IDLE/CAPTURING, 병합, stop finalize)로 확정.
- [x] dBFS 기대값 산술 → 구형/DC로 0/-20 dBFS(사인파 -3.01/-23.01)로 확정.
