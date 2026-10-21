// M3 — NVN type surface (Android host side).
//
// The guest (game) code speaks the NVN C API: it imports nvnBootstrapLoader and
// resolves every entry point through nvnDeviceGetProcAddress. The original
// Apple port intercepts exactly the same surface and re-implements it on Metal
// (games/totk/third_party/d2r_gfx/gfx/nvn/hle_nvn.c). Here the same objects are
// re-implemented on Vulkan.
//
// The structs below model the subset of NVN object layouts the shim needs on
// the HOST side. They are intentionally opaque to the guest: the guest only
// holds pointers it never dereferences, so we are free to use our own layouts
// (same approach as the original nvn_obj.h FITS() checks, minus ABI pinning).
#pragma once

#include <cstddef>
#include <cstdint>

namespace mng::nvn {

// ---- object handles (host-side structs) ------------------------------------

struct Device;
struct Queue;
struct Window;
struct MemoryPool;
struct Texture;
struct Sampler;
struct ShaderProgram;
struct Pipeline;
struct CommandBuffer;
struct Fence;
struct Event;

// NVNboolean in the real SDK is an enum-ish uint32; the original layer already
// swapped it for uint32_t in its prototypes. Keep the same convention.
using Bool = uint32_t;
constexpr Bool kTrue = 1u;
constexpr Bool kFalse = 0u;

// ---- formats ---------------------------------------------------------------
// Only the formats TOTK actually uses on NVN (subset; extend as the boot
// pipeline reveals more). Names mirror the NVN_* enum spelling.
enum class Format : int32_t {
    NONE = 0,
    R8_UNORM = 1,
    R8_SNORM = 2,
    RG8_UNORM = 5,
    RGBA8_UNORM = 16,
    RGBA8_SRGB = 17,
    BGR565 = 24,
    RGB5A1 = 25,
    RGB10A2 = 26,
    R16_UNORM = 33,
    R16_FLOAT = 40,
    RG16_FLOAT = 43,
    RGBA16_FLOAT = 49,
    R32_FLOAT = 52,
    RG32_FLOAT = 55,
    RGBA32_FLOAT = 58,
    D32_FLOAT = 64,
    D24S8_UNORM = 66,
    BC1_RGBA_UNORM = 112,
    BC1_RGBA_SRGB = 113,
    BC3_UNORM = 116,
    BC3_SRGB = 117,
    BC5_UNORM = 120,
    BC7_UNORM = 125,
    BC7_SRGB = 126,
};

// Format traits needed by the texture path (block size, whether compressed).
struct FormatInfo {
    uint32_t bytes_per_block;
    uint32_t block_width;   // 1 for uncompressed
    uint32_t block_height;  // 1 for uncompressed
    bool compressed;
};

// Returns nullptr when the format is not mapped yet — callers must treat that
// as "unsupported on Android" and record a diagnostic, never guess.
const FormatInfo* format_info(Format f);

// ---- window geometry -------------------------------------------------------

struct WindowDimensions {
    int32_t width = 0;
    int32_t height = 0;
};

// ---- diagnostics -----------------------------------------------------------

// Every shim entry point that cannot do what the game asked increments the
// matching counter instead of silently lying. The M3 probe screen surfaces
// these counts; the boot log dumps them at shutdown.
struct ShimCounters {
    uint32_t device_initialize_ok = 0;
    uint32_t device_initialize_fail = 0;
    uint32_t memory_pool_ok = 0;
    uint32_t texture_ok = 0;
    uint32_t texture_unsupported_format = 0;
    uint32_t sampler_ok = 0;
    uint32_t shader_program_ok = 0;
    uint32_t shader_program_no_spirv = 0;
    uint32_t pipeline_ok = 0;
    uint32_t command_buffer_records = 0;
    uint32_t command_buffer_unimplemented_calls = 0;
    uint32_t queue_submit_ok = 0;
    uint32_t present_ok = 0;
    uint32_t present_fail = 0;
    uint32_t get_unresolved_entry_points = 0;
};

const ShimCounters& counters();
void reset_counters();

} // namespace mng::nvn
