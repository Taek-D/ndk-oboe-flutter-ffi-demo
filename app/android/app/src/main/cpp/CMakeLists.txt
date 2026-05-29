# app/android/app/src/main/cpp/CMakeLists.txt
#
# Android 모듈이 externalNativeBuild로 링크하는 단일 최상위 CMake 스크립트.
# 책임:
#   1) Oboe를 소스로 add_subdirectory (계획서 ADR: 소스 add_subdirectory primary)
#   2) native/의 순수 로직 코어(ndk_audio_core)를 add_subdirectory (호스트 테스트는 OFF)
#   3) libndk_audio.so 빌드 + Oboe/코어/log 링크
#
# Oboe 권장 STL은 c++_shared (abiFilters x86_64는 Gradle에서 지정).

cmake_minimum_required(VERSION 3.22.1)
project(ndk_audio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 이 CMakeLists 기준 프로젝트 루트(third_party/native가 있는 E:\ndk)로 거슬러 올라간다.
#   cpp -> main -> src -> app(android모듈) -> android -> app(flutter root) -> <프로젝트 루트>
# 6단계. (이전엔 7단계 = '..' 하나 초과 → 루트가 E:\ 로 잘못 계산되어 Oboe를 못 찾았음)
get_filename_component(PROJECT_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.." ABSOLUTE)

set(OBOE_DIR "${PROJECT_ROOT}/third_party/oboe")
set(NATIVE_DIR "${PROJECT_ROOT}/native")

if(NOT EXISTS "${OBOE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
      "Oboe submodule not found at ${OBOE_DIR}. "
      "Run: git submodule update --init --recursive")
endif()

# --- Oboe (소스 빌드) ---
add_subdirectory(${OBOE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/oboe)

# --- 순수 로직 코어 (호스트와 공유, 테스트는 끈다) ---
set(NDK_AUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${NATIVE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/ndk_audio_core)

# --- libndk_audio.so ---
add_library(ndk_audio SHARED
    engine_jni.cpp
    audio_engine.cpp
)

target_include_directories(ndk_audio PRIVATE
    ${OBOE_DIR}/include
    ${NATIVE_DIR}/include
)

find_library(log-lib log)

target_link_libraries(ndk_audio
    PRIVATE
    oboe
    ndk_audio_core
    ${log-lib}
)
