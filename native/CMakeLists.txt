# native/CMakeLists.txt
#
# 이 CMake는 두 가지 컨텍스트에서 쓰인다:
#   1) PC 호스트 단위 테스트(gtest) — NDK_AUDIO_BUILD_TESTS=ON (기본 ON)
#   2) Android 모듈의 externalNativeBuild가 add_subdirectory로 포함 — 이 경우
#      상위(android/.../cpp/CMakeLists.txt)에서 NDK_AUDIO_BUILD_TESTS=OFF 로 끈다.
#
# 순수 로직(ndk_audio_core)은 양쪽에서 공통으로 컴파일된다. Oboe/JNI 의존은
# 여기 두지 않는다(호스트에서 빌드 불가하므로). 캡처/엔진/FFI는 Android 측 CMake가 담당.

cmake_minimum_required(VERSION 3.18)
project(ndk_audio_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NDK_AUDIO_BUILD_TESTS "Build host gtest suite" ON)

# --- 순수 로직 코어 라이브러리 (호스트/Android 공통) ---
# Phase 0-AM은 db_calculator만. Phase 1(#2)에서 누산기/링버퍼/SPSC/WAV 소스를
# 이 SOURCES 목록에 추가한다.
set(NDK_AUDIO_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db_calculator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db_window_accumulator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wav_writer.cpp
)

add_library(ndk_audio_core STATIC ${NDK_AUDIO_CORE_SOURCES})
target_include_directories(ndk_audio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 소스에 UTF-8 한글 주석이 있으므로 MSVC(호스트) 빌드 시 /utf-8 강제.
# (Clang/NDK는 기본 UTF-8이라 불필요.)
if(MSVC)
  target_compile_options(ndk_audio_core PRIVATE /utf-8)
endif()

# --- 호스트 gtest ---
if(NDK_AUDIO_BUILD_TESTS)
  include(FetchContent)
  # MSVC에서 gtest의 정적 CRT 충돌 방지.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
  )
  FetchContent_MakeAvailable(googletest)

  enable_testing()

  add_executable(ndk_audio_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_db_calculator.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_db_window_accumulator.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ring_buffer.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_spsc_queue.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_wav_writer.cpp
  )
  # SPSC 2-스레드 테스트용 Threads(pthread on POSIX, MSVC는 자동).
  find_package(Threads REQUIRED)
  target_link_libraries(ndk_audio_tests PRIVATE ndk_audio_core GTest::gtest_main Threads::Threads)
  if(MSVC)
    target_compile_options(ndk_audio_tests PRIVATE /utf-8)
  endif()

  include(GoogleTest)
  gtest_discover_tests(ndk_audio_tests)
endif()
