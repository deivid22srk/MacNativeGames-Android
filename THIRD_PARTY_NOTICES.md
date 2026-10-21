# Third-party code

This Android port reuses and extends code from the [MacNativeGames](https://github.com/ly05010419/MacNativeGames)
project (MIT). The upstream license file sits next to the code where applicable.
No game content of any kind is contained or distributed by this repository.

| Code | From | License | File |
|---|---|---|---|
| `docs/android-port-analysis.md`, port architecture notes | MacNativeGames upstream (analysis of its code) | MIT | `LICENSE` |
| Concepts ported incrementally (`runtime/` pure-C logic, NSO parsing rules, texture-layout notes) | MacNativeGames upstream | MIT | `LICENSE` |
| NativeAOT shader translator (`totk_xlate`) design reference | d2r-mobile, adapted upstream | MIT | upstream `games/totk/tools/totk_xlate/LICENSE` |
| `vendor/ryujinx` (Ryujinx.Graphics.Shader, Ryujinx.Common, Spv.Generator) — **not vendored here yet** | Ryujinx, as pinned by d2r-mobile | MIT | upstream `vendor/ryujinx/LICENSE.txt` |
| Audio effects porting reference (`audio_reverb`, `audio_effects`, mixer) | Ryujinx.Audio | MIT | upstream `games/totk/third_party/licenses/Ryujinx-Audio.txt` |
| Texture layout sizes (reference for the future NVN→Vulkan texture code) | deko3d | zlib | upstream `games/totk/third_party/d2r_gfx/LICENSE.deko3d` |

Fetched or compiled at build time (never committed here):

| Code | Use | License |
|---|---|---|
| Android Gradle Plugin, Gradle, Kotlin stdlib | build tooling | Apache-2.0 |
| Android NDK (r27), CMake | native toolchain | Android SDK license |
| Vulkan API headers (`vulkan/vulkan.h` via NDK sysroot) | graphics | Apache-2.0 |
| AAudio (NDK libaaudio) | audio output (M4) | Apache-2.0 |
| libopus (future, CMake/FetchContent) | Opus audio (M4) | BSD-3-Clause |
| libvpx (future, CMake/FetchContent) | VP9 movies (M4) | BSD-3-Clause |
| glslang (dev-time only, shader → SPIR-V embedded headers) | build machine tool | Apache-2.0/Khronos |
