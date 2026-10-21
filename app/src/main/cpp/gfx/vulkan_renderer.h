// M1 — Vulkan renderer for the Android port: proves the graphics stack end to
// end (instance → surface → device → swapchain → pipeline → draw loop).
//
// This is the Android counterpart of the upstream Metal layer's *shell* only.
// The real NVN→Vulkan backend (M3) will live beside it and consume SPIR-V from
// the Ryujinx shader translator; the M1 triangle uses embedded SPIR-V bytes so
// neither CI nor the device needs a shader compiler.
//
// Rendering policy notes (carried from the analysis doc):
//  - VK_API_VERSION_1_1 baseline (Vulkan 1.1 is required by the port; most
//    arm64 devices with 2019+ drivers expose it or a later version).
//  - FIFO mailbox fallback: FIFO first (no tearing, bounded latency).
//  - Depth formats are NOT used at M1 (no depth attachment in the probe).
#pragma once

#include <android/native_window.h>

#include <cstdint>
#include <vector>

namespace mng::gfx {

class VulkanRenderer {
public:
    // Initializes instance/device/swapchain/pipeline for the window. 0 = ok.
    int init(ANativeWindow* window);

    // Renders one frame. Returns monotonic frame counter.
    int render_frame();

    // Recreates the swapchain after a resize. 0 = ok.
    int resize(uint32_t width, uint32_t height);

    void shutdown();

private:
    bool create_instance();
    bool create_surface();
    bool pick_device();
    bool create_swapchain();
    bool create_render_pass();
    bool create_pipeline();
    bool create_sync();

    void destroy_swapchain();

    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace mng::gfx
