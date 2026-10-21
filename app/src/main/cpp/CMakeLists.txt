cmake_minimum_required(VERSION 3.22.1)
project(mng_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(log-lib log)
find_library(android-lib android)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
add_library(mng_android SHARED
        mng_jni.cpp
        gfx/vulkan_renderer.cpp
        runtime/nso_loader.cpp
        runtime/horizon_host.cpp
        audio/audio_mixer.cpp
        audio/audio_output_android.cpp
        video/video_player.cpp
        platform/android/platform_memory.cpp
)

target_include_directories(mng_android PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# W^X on Android: anonymous exec memory via mmap/mprotect (see platform_memory.cpp).
# 16 KB page-size support is handled by the Gradle-level
# ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON plus an explicit linker flag below.
target_link_options(mng_android PRIVATE
        "-Wl,-z,max-page-size=16384"
        "-Wl,-z,common-page-size=16384"
)

target_link_libraries(mng_android
        ${log-lib}
        ${android-lib}
        vulkan
        aaudio
)
