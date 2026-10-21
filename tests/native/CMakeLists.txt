cmake_minimum_required(VERSION 3.16)
project(mng_native_tests CXX)

# Host-side unit tests for the port's pure logic (no Android, no game data).
# CI runs: cmake -S tests/native -B build-native && ctest --test-dir build-native

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

set(MNG_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main/cpp)

add_library(mng_core STATIC
        ${MNG_CPP_DIR}/runtime/nso_loader.cpp
        ${MNG_CPP_DIR}/runtime/horizon_host.cpp
        ${MNG_CPP_DIR}/audio/audio_mixer.cpp
        ${MNG_CPP_DIR}/nvn/shader_cache.cpp
        ${MNG_CPP_DIR}/input/input_manager.cpp
        ${MNG_CPP_DIR}/diag/diag.cpp
        ${MNG_CPP_DIR}/platform/android/platform_memory.cpp
)
target_include_directories(mng_core PUBLIC ${MNG_CPP_DIR})

foreach(test_name
        test_nso_loader
        test_horizon_host
        test_audio_mixer
        test_platform_memory
        test_shader_cache
        test_input_manager
        test_diag)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE mng_core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
