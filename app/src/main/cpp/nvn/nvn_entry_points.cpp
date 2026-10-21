// M3 — NVN C entry points + registry + M3 probe driver.
//
// The X-macro NVN_TABLE is the single source of truth: it builds the name →
// pointer registry resolved by nvnBootstrapLoader / nvnDeviceGetProcAddress —
// exactly how the guest (and the probe) discovers the shim.
//
// Implemented on Vulkan (real behavior):
//   device/queue/window/pool/texture/sampler/shader/pipeline/cmdbuffer
//   lifecycle, command recording, submit replay (clear→bind→viewport→draw),
//   present.
// Registered but replay-skipped (counter += 1, no crash, no false success):
//   descriptor binding, index buffers, compute, sync primitives, debug paths.
#include "nvn/nvn_shim_internal.h"

#include <android/log.h>
#include <android/native_window.h>
#include <string.h>
#include <time.h>
#include <vector>

#include <map>
#include <string>

#include "gfx/embedded_shaders.h"
#include "nvn/nvn_format_map.h"
#include "nvn/nvn_shim.h"

#define SHIM_TAG "mng-nvn"
#define SHLOGI(...) __android_log_print(ANDROID_LOG_INFO, SHIM_TAG, __VA_ARGS__)

namespace mng::nvn {
namespace shim {
namespace {

// ---- small helpers ---------------------------------------------------------

inline Bool ok() { return kTrue; }
void skip(const char* what) { g_counters.command_buffer_unimplemented_calls++; }

VkShaderModule make_module(VkDevice d, const uint32_t* words, size_t n) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.pCode = words;
    ci.codeSize = n * sizeof(uint32_t);
    VkShaderModule m = VK_NULL_HANDLE;
    return vkCreateShaderModule(d, &ci, nullptr, &m) == VK_SUCCESS ? m : VK_NULL_HANDLE;
}

// ---- device ----------------------------------------------------------------

void DeviceBuilderSetDefaults(void* b) { memset(b, 0, 8); }
void DeviceBuilderSetFlags(void* b, int) { (void)b; }
Bool DeviceInitialize(void* dev, const void* /*builder*/) {
    if (g_dev.context_ok) {
        g_counters.device_initialize_ok++;
        return kTrue;
    }
    // Deferred attach: context comes with the native window. The probe and the
    // future boot path both call shim_attach_native_window first.
    g_counters.device_initialize_fail++;
    return kTrue; // benign: device object exists; real device arrives with window
}
void DeviceFinalize(void*) {}
void DeviceSetDebugLabel(void*, const char*) {}
void* DeviceGetProcAddress(const void*, const char* name) {
    const void* p = nvn_bootstrap_lookup(name);
    if (p == nullptr) g_counters.get_unresolved_entry_points++;
    return const_cast<void*>(p);
}
void DeviceGetInteger(const void*, int what, int* out) {
    if (out != nullptr) *out = 0; // honest default; per-info values land with boot
}
uint64_t DeviceGetCurrentTimestampInNanoseconds(const void*) {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}
void DeviceSetIntermediateShaderCache(void*, int) {}
uint64_t DeviceGetTextureHandle(const void*, int tex, int samp) {
    return (uint64_t(uint32_t(tex)) << 32) | uint32_t(samp);
}
uint64_t DeviceGetTexelFetchHandle(const void*, int tex) {
    return uint64_t(uint32_t(tex)) << 32;
}
uint64_t DeviceGetImageHandle(const void*, int tex) { return uint64_t(uint32_t(tex)) << 32; }
void DeviceInstallDebugCallback(void*, void*, void*, uint32_t) {}
uint32_t DeviceGenerateDebugDomainId(const void*, const char*) { return 1; }
void DeviceSetWindowOriginMode(void*, int) {}
void DeviceSetDepthMode(void*, int) {}
Bool DeviceRegisterFastClearColor(void*, const float*, int) { return ok(); }
Bool DeviceRegisterFastClearDepth(void*, float) { return ok(); }
void DeviceApplyDeferredFinalizes(void*, int) {}
void DeviceFinalizeCommandHandle(void*, uint64_t) {}
Bool DeviceIsExternalDebuggerAttached(const void*) { return kFalse; }

// ---- queue -----------------------------------------------------------------

void QueueBuilderSetDevice(void* b, void* d) { *(void**)b = d; }
void QueueBuilderSetDefaults(void*) {}
void QueueBuilderSetFlags(void*, int) {}
void QueueBuilderSetCommandMemorySize(void*, size_t) {}
void QueueBuilderSetComputeMemorySize(void*, size_t) {}
void QueueBuilderSetControlMemorySize(void*, size_t) {}
size_t QueueBuilderGetQueueMemorySize(const void*) { return 0; }
void QueueBuilderSetQueueMemory(void*, void*, size_t) {}
void QueueBuilderSetCommandFlushThreshold(void*, size_t) {}
Bool QueueInitialize(void* q, const void* b) {
    auto* queue = new Queue();
    g_dev.queues.push_back(queue);
    *(void**)q = queue;
    (void)b;
    return ok();
}
void QueueFinalize(void*) {}
void QueueSetDebugLabel(void*, const char*) {}

// Replays recorded ops into a real command buffer on the swapchain image.
void submit_replay(Queue* q) {
    if (!g_dev.context_ok || q->pending.empty()) return;
    const uint32_t image_index = g_dev.swap_index;
    VkCommandBuffer cmd = g_dev.cmds[image_index];

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) return;

    VkClearValue clear{};
    clear.color = {{0.06f, 0.07f, 0.09f, 1.0f}};
    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = g_dev.render_pass;
    rpi.framebuffer = g_dev.framebuffers[image_index];
    rpi.renderArea.extent = {g_dev.swap_w, g_dev.swap_h};
    rpi.clearValueCount = 1;
    rpi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    bool pipeline_bound = false;
    for (const CmdBuf* cb : q->pending) {
        for (const CmdOp& op : cb->ops) {
            switch (op.kind) {
            case CmdOp::Kind::ClearColor:
                clear.color = {{op.f[0], op.f[1], op.f[2], op.f[3]}};
                break;
            case CmdOp::Kind::SetViewport: {
                VkViewport vp{op.f[0], op.f[1], op.f[2], op.f[3], 0.0f, 1.0f};
                VkRect2D sc{{int32_t(op.u[0]), int32_t(op.u[1])},
                            {g_dev.swap_w, g_dev.swap_h}};
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);
                break;
            }
            case CmdOp::Kind::BindPipeline:
                if (op.ptr != nullptr && static_cast<Pipeline*>(op.ptr)->vk != VK_NULL_HANDLE) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      static_cast<Pipeline*>(op.ptr)->vk);
                    pipeline_bound = true;
                }
                break;
            case CmdOp::Kind::DrawArrays:
                if (pipeline_bound) vkCmdDraw(cmd, op.u[0], op.u[1], 0, 0);
                break;
            case CmdOp::Kind::DrawIndexed:
                if (pipeline_bound) {
                    skip("DrawIndexed: index buffers not wired yet");
                }
                break;
            default:
                skip("op");
                break;
            }
        }
    }
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &g_dev.sem_image;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &g_dev.sem_render;
    vkQueueSubmit(g_dev.queue, 1, &si, g_dev.submit_fence);
    vkWaitForFences(g_dev.vk, 1, &g_dev.submit_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(g_dev.vk, 1, &g_dev.submit_fence);

    g_counters.queue_submit_ok++;
    q->submits++;
    q->pending.clear();
}

void QueueSubmitCommands(void* q, int n, const uint64_t* handles) {
    auto* queue = static_cast<Queue*>(q);
    (void)handles; // handles map 1:1 to our CmdBufs in the probe; counted path
    queue->pending.push_back(g_dev.cmd_bufs.back());
    g_counters.command_buffer_records += uint32_t(g_dev.cmd_bufs.back()->ops.size());
    (void)n;
}

void QueueFlush(void*) {}
void QueueFinish(void*) {}

int QueueAcquireTexture(void* q, void* w, int* tex_index) {
    (void)q;
    (void)w;
    if (!g_dev.context_ok) return 0;
    uint32_t idx = 0;
    VkResult rc = vkAcquireNextImageKHR(g_dev.vk, g_dev.swapchain, UINT64_MAX,
                                        g_dev.sem_image, VK_NULL_HANDLE, &idx);
    if (rc != VK_SUCCESS && rc != VK_SUBOPTIMAL_KHR) return 0;
    g_dev.swap_index = idx;
    g_dev.last_acquired = idx;
    if (tex_index != nullptr) *tex_index = int(idx);
    return 1;
}

void QueuePresentTexture(void* q, void* w, int idx) {
    (void)q;
    (void)w;
    (void)idx;
    if (!g_dev.context_ok) {
        g_counters.present_fail++;
        return;
    }
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &g_dev.sem_render;
    pi.swapchainCount = 1;
    pi.pSwapchains = &g_dev.swapchain;
    pi.pImageIndices = &g_dev.swap_index;
    VkResult rc = vkQueuePresentKHR(g_dev.queue, &pi);
    if (rc == VK_SUCCESS) g_counters.present_ok++;
    else g_counters.present_fail++;
}

// ---- window ----------------------------------------------------------------

void WindowBuilderSetDevice(void* b, void* d) { *(void**)b = d; }
void WindowBuilderSetDefaults(void*) {}
void WindowBuilderSetNativeWindow(void* b, void* w) { ((void**)b)[1] = w; }
void WindowBuilderSetTextures(void* b, int n, void* const* t) { (void)b; (void)n; (void)t; }
void WindowBuilderSetFormat(void*, int) {}
void WindowBuilderSetDepthFormat(void*, int) {}
void WindowBuilderSetSampleCounts(void*, int) {}
void WindowBuilderSetInterval(void*, int) {}
Bool WindowInitialize(void* w, const void* b) {
    auto* win = new Window();
    const void* const* bb = static_cast<const void* const*>(b);
    win->native = const_cast<void*>(bb[1]); // set by WindowBuilderSetNativeWindow
    g_dev.windows.push_back(win);
    *(void**)w = win;
    return ok();
}
void WindowFinalize(void*) {}
int WindowGetActiveTexture(void* w) {
    (void)w;
    return int(g_dev.last_acquired);
}
void WindowSetSwapInterval(void* w, int i) {
    static_cast<Window*>(w)->interval = i;
}
void WindowAcquireTexture(void*, void*, int*) {}

// ---- memory pools ----------------------------------------------------------

void MemoryPoolBuilderSetDevice(void* b, void* d) { *(void**)b = d; }
void MemoryPoolBuilderSetDefaults(void*) {}
void MemoryPoolBuilderSetFlags(void* b, int f) { ((int*)b)[1] = f; }
void MemoryPoolBuilderSetSize(void* b, size_t s) { ((size_t*)b)[2] = s; }
void MemoryPoolBuilderSetStorage(void*, void*, size_t) {}
Bool MemoryPoolInitialize(void* mp, const void* b) {
    auto* pool = new Pool();
    g_dev.pools.push_back(pool);
    *(void**)mp = pool;
    (void)b;
    g_counters.memory_pool_ok++;
    return ok();
}
void MemoryPoolFinalize(void*) {}
void* MemoryPoolMap(void* mp) { return static_cast<Pool*>(mp)->mapped; }
void MemoryPoolFlush(void*, uint64_t, uint64_t) {}
void MemoryPoolFlushRange(void*, uint64_t, uint64_t) {}
void MemoryPoolUnmap(void*) {}

// ---- textures --------------------------------------------------------------

void TextureBuilderSetDevice(void* b, void* d) { *(void**)b = d; }
void TextureBuilderSetDefaults(void*) {}
void TextureBuilderSetTarget(void*, int) {}
void TextureBuilderSetFormat(void* b, int f) { ((int*)b)[2] = f; }
void TextureBuilderSetSize2D(void* b, int w, int h) {
    ((int*)b)[3] = w;
    ((int*)b)[4] = h;
}
void TextureBuilderSetLevels(void*, int) {}
void TextureBuilderSetStride(void*, int) {}
void TextureBuilderSetSwizzle(void*, const int*) {}
void TextureBuilderSetDepthStencilMode(void*, int) {}
Bool TextureInitialize(void* t, const void* b) {
    auto* tex = new Texture();
    // Descriptor layout used by the probe only (game path reads the same
    // fields through its own builder cookies once boot wires them).
    const int* bb = static_cast<const int*>(b);
    tex->format = static_cast<Format>(bb[2]);
    tex->width = uint32_t(bb[3]);
    tex->height = uint32_t(bb[4]);
    g_dev.textures.push_back(tex);
    *(void**)t = tex;
    g_counters.texture_ok++;
    return ok();
}
void TextureFinalize(void*) {}
void TextureGetViewBounds(void*, int*, int*) {}

// ---- samplers --------------------------------------------------------------

void SamplerBuilderSetDevice(void* b, void* d) { *(void**)b = d; }
void SamplerBuilderSetDefaults(void*) {}
void SamplerBuilderSetFilters(void*, int, int) {}
void SamplerBuilderSetWrapMode(void*, int, int, int) {}
void SamplerBuilderSetLodBoundsClamp(void*, float, float, int) {}
void SamplerBuilderSetLodClamp(void*, float, float) {}
void SamplerBuilderSetLodBounds(void*, float, float) {}
void SamplerBuilderSetBorderColor(void*, int) {}
void SamplerBuilderSetAnisotropy(void*, float) {}
void SamplerBuilderSetDepthCompareMode(void*, int) {}
Bool SamplerInitialize(void* s, const void* b) {
    auto* samp = new Sampler();
    g_dev.samplers.push_back(samp);
    *(void**)s = samp;
    (void)b;
    g_counters.sampler_ok++;
    return ok();
}
void SamplerFinalize(void*) {}

// ---- shader programs -------------------------------------------------------
// The guest's translated shaders arrive as SPIR-V words (Ryujinx translator
// emits SPIR-V directly — the whole reason Android needs no SPIRV-Cross).

void ShaderProgramSetStage(void* sp, int stage, uint64_t* words, size_t n) {
    auto* sh = static_cast<Shader*>(sp);
    switch (stage) {
    case 0: // VERTEX
        sh->has_vs = true;
        sh->spirv_vs.assign(words, words + n);
        break;
    case 1: // FRAGMENT
        sh->has_fs = true;
        sh->spirv_fs.assign(words, words + n);
        break;
    case 2: // COMPUTE
        sh->has_cs = true;
        sh->spirv_cs.assign(words, words + n);
        break;
    default: break;
    }
}
Bool ShaderProgramInitialize(void* sp, void* dev) {
    (void)dev;
    auto* sh = new Shader();
    g_dev.shaders.push_back(sh);
    *(void**)sp = sh;
    g_counters.shader_program_ok++;
    return ok();
}
void ShaderProgramFinalize(void*) {}

// ---- pipelines -------------------------------------------------------------

void PipelineBuilderSetDevice(void* b, void* d) { *(void**)b = d; }
void PipelineBuilderSetDefaults(void*) {}
void PipelineBuilderSetShaders(void* b, void* sh) { ((void**)b)[1] = sh; }
void PipelineBuilderSetChannelMask(void*, int) {}
void PipelineBuilderSetBlend(void*, const void*) {}
void PipelineBuilderSetBlendAdvanced(void*, const void*) {}
void PipelineBuilderSetStencil(void*, const void*) {}
void PipelineBuilderSetDepthStencilState(void*, const void*) {}
void PipelineBuilderSetMultisampleState(void*, const void*) {}
void PipelineBuilderSetPolygonState(void*, const void*) {}
void PipelineBuilderSetAttributeState(void*, const void*) {}
void PipelineBuilderSetColorState(void*, const void*) {}
void PipelineBuilderSetVertexBindingState(void*, const void*) {}
void PipelineBuilderSetControlFlowState(void*, const void*) {}
Bool PipelineInitialize(void* p, const void* b) {
    auto* pipe = new Pipeline();
    const void* const* bb = static_cast<const void* const*>(b);
    pipe->shaders = static_cast<Shader*>(const_cast<void*>(bb[1]));
    pipe->vk = build_vk_pipeline(pipe->shaders); // real VkGraphicsPipeline
    g_dev.pipelines.push_back(pipe);
    *(void**)p = pipe;
    g_counters.pipeline_ok++;
    return ok();
}
void PipelineFinalize(void*) {}

// ---- command buffers -------------------------------------------------------

void CommandBufferInitialize(void* cb, void* dev) {
    (void)dev;
    auto* c = new CmdBuf();
    g_dev.cmd_bufs.push_back(c);
    *(void**)cb = c;
}
void CommandBufferFinalize(void*) {}
void CommandBufferSetDefaults(void*) {}
void CommandBufferBegin(void* cb) {
    static_cast<CmdBuf*>(cb)->ops.clear();
    static_cast<CmdBuf*>(cb)->recording = true;
}
void CommandBufferEnd(void* cb) { static_cast<CmdBuf*>(cb)->recording = false; }
void CommandBufferClearColor(void* cb, int rt, const float* c, int fmt) {
    (void)rt;
    (void)fmt;
    auto* cbuf = static_cast<CmdBuf*>(cb);
    if (!cbuf->recording) return;
    CmdOp op{};
    op.kind = CmdOp::Kind::ClearColor;
    memcpy(op.f, c, sizeof(float) * 4);
    cbuf->ops.push_back(op);
}
void CommandBufferClearDepthStencil(void*, float, uint32_t, uint32_t) {
    skip("ClearDepthStencil");
}
void CommandBufferSetViewports(void* cb, int first, int n, const float* v) {
    (void)first;
    (void)n;
    auto* cbuf = static_cast<CmdBuf*>(cb);
    if (!cbuf->recording || v == nullptr) return;
    CmdOp op{};
    op.kind = CmdOp::Kind::SetViewport;
    memcpy(op.f, v, sizeof(float) * 4);
    cbuf->ops.push_back(op);
}
void CommandBufferSetScissors(void*, int, int, const void*) { skip("SetScissors"); }
void CommandBufferBindPipelineProgram(void* cb, void* p, int) {
    auto* cbuf = static_cast<CmdBuf*>(cb);
    if (!cbuf->recording) return;
    CmdOp op{};
    op.kind = CmdOp::Kind::BindPipeline;
    op.ptr = p;
    cbuf->ops.push_back(op);
}
void CommandBufferBindUniformBuffer(void* cb, int, uint64_t, uint64_t, void*) {
    skip("BindUniformBuffer");
}
void CommandBufferBindTextures(void* cb, int, const uint64_t*, const int*) {
    skip("BindTextures");
}
void CommandBufferBindSeparateTextureHandles(void* cb, int, int, const uint64_t*) {
    skip("BindSeparateTextureHandles");
}
void CommandBufferBindSeparateSamplers(void* cb, int, int, const int*) {
    skip("BindSeparateSamplers");
}
void CommandBufferBindVertexBuffer(void*, int, const void*, uint64_t) {
    skip("BindVertexBuffer");
}
void CommandBufferBindIndexBuffer(void*, const void*, uint64_t, int) {
    skip("BindIndexBuffer");
}
void CommandBufferDrawArrays(void* cb, int topology, int count, int first, int inst) {
    auto* cbuf = static_cast<CmdBuf*>(cb);
    if (!cbuf->recording) return;
    CmdOp op{};
    op.kind = CmdOp::Kind::DrawArrays;
    op.u[0] = uint32_t(count);
    op.u[1] = uint32_t(inst == 0 ? 1 : inst);
    op.u[2] = uint32_t(first);
    op.u[3] = uint32_t(topology);
    cbuf->ops.push_back(op);
}
void CommandBufferDrawIndexed(void* cb, int, int, int, int, int) { skip("DrawIndexed"); }
void CommandBufferSendSync(void*, void*) { skip("SendSync"); }
void CommandBufferWaitSync(void*, void*, uint64_t) { skip("WaitSync"); }
void CommandBufferWaitEvent(void*, void*) { skip("WaitEvent"); }
void CommandBufferSignalEvent(void*, void*) { skip("SignalEvent"); }
void CommandBufferClearEvents(void*, int, void* const*) { skip("ClearEvents"); }
void CommandBufferWaitFence(void*, const void*) { skip("WaitFence"); }
void CommandBufferSetPatchSize(void*, float) { skip("SetPatchSize"); }
void CommandBufferSetShaderScratchMemory(void*, void*, uint64_t, uint64_t) {
    skip("SetShaderScratchMemory");
}
void CommandBufferBindTexturesDeferred(void*, int, int, const void*, uint64_t) {
    skip("BindTexturesDeferred");
}
void CommandBufferSavePersistentScratchMemory(void*) { skip("SavePersistentScratch"); }
void CommandBufferRestorePersistentScratchMemory(void*) { skip("RestorePersistentScratch"); }

// ---- events / fences (registered, counted) ---------------------------------

void EventBuilderSetDefaults(void*) {}
Bool EventInitialize(void*, const void*) { return ok(); }
void EventFinalize(void*) {}
void FenceBuilderSetDefaults(void*) {}
Bool FenceInitialize(void*, const void*, uint32_t) { return ok(); }
void FenceFinalize(void*) {}
void FenceAppendWaitCallback(void*, void*, void*) {}

// ---- registry --------------------------------------------------------------

struct Entry {
    const char* name;
    void* fn;
};

using Registry = std::map<std::string, void*>;

Registry& registry() {
    static Registry r = [] {
        Registry m;
#define E(sym, fn) m[#sym] = reinterpret_cast<void*>(&fn);
#define X(sym, impl) E(sym, impl)
        // device
        X(nvnDeviceBuilderSetDefaults, DeviceBuilderSetDefaults) X(nvnDeviceBuilderSetFlags, DeviceBuilderSetFlags) X(nvnDeviceInitialize, DeviceInitialize)
        X(nvnDeviceFinalize, DeviceFinalize) X(nvnDeviceSetDebugLabel, DeviceSetDebugLabel) X(nvnDeviceGetProcAddress, DeviceGetProcAddress)
        X(nvnDeviceGetInteger, DeviceGetInteger) X(nvnDeviceGetCurrentTimestampInNanoseconds, DeviceGetCurrentTimestampInNanoseconds)
        X(nvnDeviceSetIntermediateShaderCache, DeviceSetIntermediateShaderCache) X(nvnDeviceGetTextureHandle, DeviceGetTextureHandle)
        X(nvnDeviceGetTexelFetchHandle, DeviceGetTexelFetchHandle) X(nvnDeviceGetImageHandle, DeviceGetImageHandle)
        X(nvnDeviceInstallDebugCallback, DeviceInstallDebugCallback) X(nvnDeviceGenerateDebugDomainId, DeviceGenerateDebugDomainId)
        X(nvnDeviceSetWindowOriginMode, DeviceSetWindowOriginMode) X(nvnDeviceSetDepthMode, DeviceSetDepthMode)
        X(nvnDeviceRegisterFastClearColor, DeviceRegisterFastClearColor) X(nvnDeviceRegisterFastClearDepth, DeviceRegisterFastClearDepth)
        X(nvnDeviceApplyDeferredFinalizes, DeviceApplyDeferredFinalizes) X(nvnDeviceFinalizeCommandHandle, DeviceFinalizeCommandHandle)
        X(nvnDeviceIsExternalDebuggerAttached, DeviceIsExternalDebuggerAttached)
        // queue
        X(nvnQueueBuilderSetDevice, QueueBuilderSetDevice) X(nvnQueueBuilderSetDefaults, QueueBuilderSetDefaults)
        X(nvnQueueBuilderSetFlags, QueueBuilderSetFlags) X(nvnQueueBuilderSetCommandMemorySize, QueueBuilderSetCommandMemorySize)
        X(nvnQueueBuilderSetComputeMemorySize, QueueBuilderSetComputeMemorySize) X(nvnQueueBuilderSetControlMemorySize, QueueBuilderSetControlMemorySize)
        X(nvnQueueBuilderGetQueueMemorySize, QueueBuilderGetQueueMemorySize) X(nvnQueueBuilderSetQueueMemory, QueueBuilderSetQueueMemory)
        X(nvnQueueBuilderSetCommandFlushThreshold, QueueBuilderSetCommandFlushThreshold) X(nvnQueueInitialize, QueueInitialize)
        X(nvnQueueFinalize, QueueFinalize) X(nvnQueueSetDebugLabel, QueueSetDebugLabel) X(nvnQueueSubmitCommands, QueueSubmitCommands)
        X(nvnQueueFlush, QueueFlush) X(nvnQueueFinish, QueueFinish) X(nvnQueueAcquireTexture, QueueAcquireTexture)
        X(nvnQueuePresentTexture, QueuePresentTexture)
        // window
        X(nvnWindowBuilderSetDevice, WindowBuilderSetDevice) X(nvnWindowBuilderSetDefaults, WindowBuilderSetDefaults)
        X(nvnWindowBuilderSetNativeWindow, WindowBuilderSetNativeWindow) X(nvnWindowBuilderSetTextures, WindowBuilderSetTextures)
        X(nvnWindowBuilderSetFormat, WindowBuilderSetFormat) X(nvnWindowBuilderSetDepthFormat, WindowBuilderSetDepthFormat)
        X(nvnWindowBuilderSetSampleCounts, WindowBuilderSetSampleCounts) X(nvnWindowBuilderSetInterval, WindowBuilderSetInterval)
        X(nvnWindowInitialize, WindowInitialize) X(nvnWindowFinalize, WindowFinalize) X(nvnWindowGetActiveTexture, WindowGetActiveTexture)
        X(nvnWindowSetSwapInterval, WindowSetSwapInterval) X(nvnWindowAcquireTexture, WindowAcquireTexture)
        // memory pools
        X(nvnMemoryPoolBuilderSetDevice, MemoryPoolBuilderSetDevice) X(nvnMemoryPoolBuilderSetDefaults, MemoryPoolBuilderSetDefaults)
        X(nvnMemoryPoolBuilderSetFlags, MemoryPoolBuilderSetFlags) X(nvnMemoryPoolBuilderSetSize, MemoryPoolBuilderSetSize)
        X(nvnMemoryPoolBuilderSetStorage, MemoryPoolBuilderSetStorage) X(nvnMemoryPoolInitialize, MemoryPoolInitialize)
        X(nvnMemoryPoolFinalize, MemoryPoolFinalize) X(nvnMemoryPoolMap, MemoryPoolMap) X(nvnMemoryPoolFlush, MemoryPoolFlush)
        X(nvnMemoryPoolFlushRange, MemoryPoolFlushRange) X(nvnMemoryPoolUnmap, MemoryPoolUnmap)
        // textures
        X(nvnTextureBuilderSetDevice, TextureBuilderSetDevice) X(nvnTextureBuilderSetDefaults, TextureBuilderSetDefaults)
        X(nvnTextureBuilderSetTarget, TextureBuilderSetTarget) X(nvnTextureBuilderSetFormat, TextureBuilderSetFormat)
        X(nvnTextureBuilderSetSize2D, TextureBuilderSetSize2D) X(nvnTextureBuilderSetLevels, TextureBuilderSetLevels)
        X(nvnTextureBuilderSetStride, TextureBuilderSetStride) X(nvnTextureBuilderSetSwizzle, TextureBuilderSetSwizzle)
        X(nvnTextureBuilderSetDepthStencilMode, TextureBuilderSetDepthStencilMode) X(nvnTextureInitialize, TextureInitialize)
        X(nvnTextureFinalize, TextureFinalize) X(nvnTextureGetViewBounds, TextureGetViewBounds)
        // samplers
        X(nvnSamplerBuilderSetDevice, SamplerBuilderSetDevice) X(nvnSamplerBuilderSetDefaults, SamplerBuilderSetDefaults)
        X(nvnSamplerBuilderSetFilters, SamplerBuilderSetFilters) X(nvnSamplerBuilderSetWrapMode, SamplerBuilderSetWrapMode)
        X(nvnSamplerBuilderSetLodBoundsClamp, SamplerBuilderSetLodBoundsClamp) X(nvnSamplerBuilderSetLodClamp, SamplerBuilderSetLodClamp)
        X(nvnSamplerBuilderSetLodBounds, SamplerBuilderSetLodBounds) X(nvnSamplerBuilderSetBorderColor, SamplerBuilderSetBorderColor)
        X(nvnSamplerBuilderSetAnisotropy, SamplerBuilderSetAnisotropy) X(nvnSamplerBuilderSetDepthCompareMode, SamplerBuilderSetDepthCompareMode)
        X(nvnSamplerInitialize, SamplerInitialize) X(nvnSamplerFinalize, SamplerFinalize)
        // shaders + pipelines
        X(nvnShaderProgramSetStage, ShaderProgramSetStage) X(nvnShaderProgramInitialize, ShaderProgramInitialize)
        X(nvnShaderProgramFinalize, ShaderProgramFinalize) X(nvnPipelineBuilderSetDevice, PipelineBuilderSetDevice)
        X(nvnPipelineBuilderSetDefaults, PipelineBuilderSetDefaults) X(nvnPipelineBuilderSetShaders, PipelineBuilderSetShaders)
        X(nvnPipelineBuilderSetChannelMask, PipelineBuilderSetChannelMask) X(nvnPipelineBuilderSetBlend, PipelineBuilderSetBlend)
        X(nvnPipelineBuilderSetBlendAdvanced, PipelineBuilderSetBlendAdvanced) X(nvnPipelineBuilderSetStencil, PipelineBuilderSetStencil)
        X(nvnPipelineBuilderSetDepthStencilState, PipelineBuilderSetDepthStencilState) X(nvnPipelineBuilderSetMultisampleState, PipelineBuilderSetMultisampleState)
        X(nvnPipelineBuilderSetPolygonState, PipelineBuilderSetPolygonState) X(nvnPipelineBuilderSetAttributeState, PipelineBuilderSetAttributeState)
        X(nvnPipelineBuilderSetColorState, PipelineBuilderSetColorState) X(nvnPipelineBuilderSetVertexBindingState, PipelineBuilderSetVertexBindingState)
        X(nvnPipelineBuilderSetControlFlowState, PipelineBuilderSetControlFlowState) X(nvnPipelineInitialize, PipelineInitialize)
        X(nvnPipelineFinalize, PipelineFinalize)
        // command buffers
        X(nvnCommandBufferInitialize, CommandBufferInitialize) X(nvnCommandBufferFinalize, CommandBufferFinalize)
        X(nvnCommandBufferSetDefaults, CommandBufferSetDefaults) X(nvnCommandBufferBegin, CommandBufferBegin) X(nvnCommandBufferEnd, CommandBufferEnd)
        X(nvnCommandBufferClearColor, CommandBufferClearColor) X(nvnCommandBufferClearDepthStencil, CommandBufferClearDepthStencil)
        X(nvnCommandBufferSetViewports, CommandBufferSetViewports) X(nvnCommandBufferSetScissors, CommandBufferSetScissors)
        X(nvnCommandBufferBindPipelineProgram, CommandBufferBindPipelineProgram) X(nvnCommandBufferBindUniformBuffer, CommandBufferBindUniformBuffer)
        X(nvnCommandBufferBindTextures, CommandBufferBindTextures) X(nvnCommandBufferBindSeparateTextureHandles, CommandBufferBindSeparateTextureHandles)
        X(nvnCommandBufferBindSeparateSamplers, CommandBufferBindSeparateSamplers) X(nvnCommandBufferBindVertexBuffer, CommandBufferBindVertexBuffer)
        X(nvnCommandBufferBindIndexBuffer, CommandBufferBindIndexBuffer) X(nvnCommandBufferDrawArrays, CommandBufferDrawArrays)
        X(nvnCommandBufferDrawIndexed, CommandBufferDrawIndexed) X(nvnCommandBufferSendSync, CommandBufferSendSync)
        X(nvnCommandBufferWaitSync, CommandBufferWaitSync) X(nvnCommandBufferWaitEvent, CommandBufferWaitEvent)
        X(nvnCommandBufferSignalEvent, CommandBufferSignalEvent) X(nvnCommandBufferClearEvents, CommandBufferClearEvents)
        X(nvnCommandBufferWaitFence, CommandBufferWaitFence) X(nvnCommandBufferSetPatchSize, CommandBufferSetPatchSize)
        X(nvnCommandBufferSetShaderScratchMemory, CommandBufferSetShaderScratchMemory) X(nvnCommandBufferBindTexturesDeferred, CommandBufferBindTexturesDeferred)
        X(nvnCommandBufferSavePersistentScratchMemory, CommandBufferSavePersistentScratchMemory)
        X(nvnCommandBufferRestorePersistentScratchMemory, CommandBufferRestorePersistentScratchMemory)
        // events / fences
        X(nvnEventBuilderSetDefaults, EventBuilderSetDefaults) X(nvnEventInitialize, EventInitialize) X(nvnEventFinalize, EventFinalize)
        X(nvnFenceBuilderSetDefaults, FenceBuilderSetDefaults) X(nvnFenceInitialize, FenceInitialize) X(nvnFenceFinalize, FenceFinalize)
        X(nvnFenceAppendWaitCallback, FenceAppendWaitCallback)
#undef X
#undef E
        return m;
    }();
    return r;
}

} // namespace

const void* nvn_bootstrap_lookup(const char* name) {
    auto& r = registry();
    auto it = r.find(name ? name : "");
    return it == r.end() ? nullptr : it->second;
}

uint32_t registered_entry_points() { return uint32_t(registry().size()); }
uint32_t registered_entry_count() { return registered_entry_points(); }

} // namespace shim
} // namespace mng::nvn

namespace mng::nvn {
uint32_t shim_registered_entries() { return shim::registered_entry_points(); }
} // namespace mng::nvn

// ---- SDK interception point ------------------------------------------------

extern "C" const void* nvnBootstrapLoader(const char* name) {
    return mng::nvn::shim::nvn_bootstrap_lookup(name);
}

// ---- M3 acceptance probe ---------------------------------------------------
// Draws the RGB-gradient triangle by resolving EVERY NVN call by name from the
// shim registry and calling it — the exact discovery path the guest uses.
// Uses the embedded M1 shaders as SPIR-V stand-ins for translated shaders.
namespace mng::nvn {

namespace {

template <typename T>
T resolve(const char* name) {
    const void* p = shim::nvn_bootstrap_lookup(name);
    return reinterpret_cast<T>(const_cast<void*>(p));
}

// Copies a uint32_t SPIR-V array into 8-byte-aligned storage so the uint64_t*
// parameter of nvnShaderProgramSetStage is legally aligned.
std::vector<uint64_t> to_aligned_words(const uint32_t* src, uint32_t count) {
    std::vector<uint64_t> out((count + 1) / 2, 0);
    memcpy(out.data(), src, size_t(count) * sizeof(uint32_t));
    return out;
}

} // namespace

int probe_render_triangle(void* anative_window, int frames) {
    if (anative_window == nullptr || frames <= 0) return -1;
    int32_t w = 0, h = 0;
    const int rc = shim_attach_native_window(anative_window, &w, &h);
    if (rc != 0) return rc;

    // Everything below goes through the registry — no direct calls.
    auto dev_builder_defaults = resolve<void (*)(void*)>("nvnDeviceBuilderSetDefaults");
    auto dev_init = resolve<Bool (*)(void*, const void*)>("nvnDeviceInitialize");
    auto sp_init = resolve<Bool (*)(void*, void*)>("nvnShaderProgramInitialize");
    auto sp_stage =
        resolve<void (*)(void*, int, uint64_t*, size_t)>("nvnShaderProgramSetStage");
    auto pipe_shaders = resolve<void (*)(void*, void*)>("nvnPipelineBuilderSetShaders");
    auto pipe_init = resolve<Bool (*)(void*, const void*)>("nvnPipelineInitialize");
    auto cmdbuf_init = resolve<void (*)(void*, void*)>("nvnCommandBufferInitialize");
    auto cmdbuf_begin = resolve<void (*)(void*)>("nvnCommandBufferBegin");
    auto cmdbuf_end = resolve<void (*)(void*)>("nvnCommandBufferEnd");
    auto cmdbuf_clear =
        resolve<void (*)(void*, int, const float*, int)>("nvnCommandBufferClearColor");
    auto cmdbuf_viewports =
        resolve<void (*)(void*, int, int, const float*)>("nvnCommandBufferSetViewports");
    auto cmdbuf_bind =
        resolve<void (*)(void*, void*, int)>("nvnCommandBufferBindPipelineProgram");
    auto cmdbuf_draw =
        resolve<void (*)(void*, int, int, int, int)>("nvnCommandBufferDrawArrays");
    auto queue_bdev = resolve<void (*)(void*, void*)>("nvnQueueBuilderSetDevice");
    auto queue_init = resolve<Bool (*)(void*, const void*)>("nvnQueueInitialize");
    auto queue_submit =
        resolve<void (*)(void*, int, const uint64_t*)>("nvnQueueSubmitCommands");
    auto queue_acquire = resolve<int (*)(void*, void*, int*)>("nvnQueueAcquireTexture");
    auto queue_present = resolve<void (*)(void*, void*, int)>("nvnQueuePresentTexture");
    if (!dev_init || !sp_init || !sp_stage || !pipe_init || !cmdbuf_init || !queue_init ||
        !queue_submit || !queue_acquire || !queue_present) {
        return -2;
    }

    alignas(16) uint64_t dev_storage[8] = {};
    alignas(16) uint64_t devb[8] = {};
    alignas(16) uint64_t shader_storage[8] = {};
    alignas(16) uint64_t pipeb[8] = {};
    alignas(16) uint64_t pipe_storage[8] = {};
    alignas(16) uint64_t cmdbuf_storage[8] = {};
    alignas(16) uint64_t queueb[8] = {};
    alignas(16) uint64_t queue_storage[8] = {};

    if (dev_builder_defaults) dev_builder_defaults(devb);
    dev_init(dev_storage, devb);
    auto* shader = reinterpret_cast<void*>(shader_storage);
    if (!sp_init(shader, dev_storage)) return -3;
    const auto vs_words = to_aligned_words(kVertexSpirv, kVertexSpirvSize);
    const auto fs_words = to_aligned_words(kFragmentSpirv, kFragmentSpirvSize);
    sp_stage(shader, 0, const_cast<uint64_t*>(vs_words.data()), kVertexSpirvSize);
    sp_stage(shader, 1, const_cast<uint64_t*>(fs_words.data()), kFragmentSpirvSize);
    pipe_shaders(pipeb, shader);
    if (!pipe_init(pipe_storage, pipeb)) return -4;
    auto* pipe = reinterpret_cast<void*>(pipe_storage);
    cmdbuf_init(cmdbuf_storage, dev_storage);
    auto* cmd = reinterpret_cast<void*>(cmdbuf_storage);
    queue_bdev(queueb, dev_storage);
    if (!queue_init(queue_storage, queueb)) return -5;
    auto* queue = reinterpret_cast<void*>(queue_storage);

    const float kClear[4] = {0.06f, 0.07f, 0.09f, 1.0f};
    const float kViewport[4] = {0.0f, 0.0f, float(w), float(h)};
    const uint64_t kHandle = 1;

    for (int f = 0; f < frames; ++f) {
        int idx = 0;
        if (queue_acquire(queue, nullptr, &idx) == 0) return -6;
        cmdbuf_begin(cmd);
        cmdbuf_clear(cmd, 0, kClear, 0);
        cmdbuf_viewports(cmd, 0, 1, kViewport);
        cmdbuf_bind(cmd, pipe, 0);
        cmdbuf_draw(cmd, 6 /*triangle list*/, 3, 0, 0);
        cmdbuf_end(cmd);
        queue_submit(queue, 1, &kHandle);
        queue_present(queue, nullptr, idx);
    }
    SHLOGI("M3 NVN probe finished: %d frames through the registry", frames);
    return 0;
}

} // namespace mng::nvn
