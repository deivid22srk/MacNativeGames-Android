// M3 — internal state shared between nvn_shim.cpp (context) and
// nvn_entry_points.cpp (C API table). Not part of the public surface.
#pragma once

#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif
#include <vulkan/vulkan.h>

#include <mutex>
#include <vector>

#include "nvn/nvn_types.h"

namespace mng::nvn {
namespace shim {

struct Pool {
    uint64_t size = 0;
    uint32_t flags = 0;
    void* mapped = nullptr;
};

struct Texture {
    Format format = Format::NONE;
    uint32_t width = 0, height = 0, levels = 1;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    bool created = false;
};

struct Sampler {
    VkSampler vk = VK_NULL_HANDLE;
};

struct Shader {
    bool has_vs = false, has_fs = false, has_cs = false;
    std::vector<uint32_t> spirv_vs, spirv_fs, spirv_cs;
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE, cs = VK_NULL_HANDLE;
};

struct Pipeline {
    Shader* shaders = nullptr;
    VkPipeline vk = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

struct CmdOp {
    enum class Kind : uint32_t {
        ClearColor = 0,
        SetViewport,
        BindPipeline,
        DrawArrays,
        DrawIndexed,
        BindTextures,
        BindUniform,
    };
    Kind kind;
    float f[8] = {};
    uint32_t u[8] = {};
    void* ptr = nullptr;
};

struct CmdBuf {
    std::vector<CmdOp> ops;
    bool recording = false;
};

struct Queue {
    std::vector<CmdBuf*> pending;
    uint64_t submits = 0;
};

struct Window {
    void* native = nullptr; // ANativeWindow*
    int32_t interval = 1;
    int32_t acquired = 0;
};

struct Device {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice vk = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swap_images;
    std::vector<VkImageView> swap_views;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkCommandBuffer> cmds;
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFormat swap_format = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t swap_w = 0, swap_h = 0;
    uint32_t swap_index = 0;
    VkSemaphore sem_image = VK_NULL_HANDLE;
    VkSemaphore sem_render = VK_NULL_HANDLE;
    VkFence submit_fence = VK_NULL_HANDLE;
    bool context_ok = false;
    // Bound state for the submit replay.
    Pipeline* bound_pipeline = nullptr;
    uint32_t last_acquired = 0;

    std::vector<Pool*> pools;
    std::vector<Texture*> textures;
    std::vector<Sampler*> samplers;
    std::vector<Shader*> shaders;
    std::vector<Pipeline*> pipelines;
    std::vector<CmdBuf*> cmd_bufs;
    std::vector<Queue*> queues;
    std::vector<Window*> windows;
};

extern Device g_dev;
extern std::mutex g_mu;
extern ShimCounters g_counters;

// Defined in nvn_shim.cpp: realizes a Vulkan graphics pipeline from a recorded
// Shader (SPIR-V vs+fs). Returns VK_NULL_HANDLE when the context is down or
// the shader lacks either stage.
VkPipeline build_vk_pipeline(Shader* sh);

// Helpers shared with the public attach path (defined in nvn_shim.cpp).
bool create_swapchain_resources(Device& d);
VkShaderModule make_module(VkDevice d, const uint32_t* words, size_t n);

// Registry lookup shared with the entry-point file; implemented there.
const void* nvn_bootstrap_lookup(const char* name);
uint32_t registered_entry_points();

} // namespace shim
} // namespace mng::nvn
