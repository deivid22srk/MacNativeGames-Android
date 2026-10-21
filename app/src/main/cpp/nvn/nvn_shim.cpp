// M3 — NVN→Vulkan shim: Vulkan context + shared object state.
// The C entry points live in nvn_entry_points.cpp; this file owns the VkDevice
// context, the shared Device state and the pipeline builder.
#include "nvn/nvn_shim_internal.h"

#include <android/log.h>
#include <android/native_window.h>
#include <string.h>

#include "nvn/nvn_shim.h"

#define SHIM_TAG "mng-nvn"
#define SHLOGI(...) __android_log_print(ANDROID_LOG_INFO, SHIM_TAG, __VA_ARGS__)

namespace mng::nvn {

// ---- shared shim state (mng::nvn::shim) ------------------------------------

namespace shim {

Device g_dev;
std::mutex g_mu;
ShimCounters g_counters;

// Shared helpers (declared in nvn_shim_internal.h so the public attach path
// can reach them).
VkShaderModule make_module(VkDevice d, const uint32_t* words, size_t n) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.pCode = words;
    ci.codeSize = n * sizeof(uint32_t);
    VkShaderModule m = VK_NULL_HANDLE;
    return vkCreateShaderModule(d, &ci, nullptr, &m) == VK_SUCCESS ? m : VK_NULL_HANDLE;
}

bool create_swapchain_resources(Device& d) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(d.physical, d.surface, &caps);
    d.swap_w = caps.currentExtent.width;
    d.swap_h = caps.currentExtent.height;

    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(d.physical, d.surface, &fmt_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(d.physical, d.surface, &fmt_count, formats.data());
    if (!formats.empty()) d.swap_format = formats[0].format;

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = d.surface;
    sci.minImageCount = image_count;
    sci.imageFormat = d.swap_format;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = caps.currentExtent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(d.vk, &sci, nullptr, &d.swapchain) != VK_SUCCESS) return false;

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(d.vk, d.swapchain, &n, nullptr);
    d.swap_images.resize(n);
    vkGetSwapchainImagesKHR(d.vk, d.swapchain, &n, d.swap_images.data());
    d.swap_views.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = d.swap_images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = d.swap_format;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(d.vk, &vci, nullptr, &d.swap_views[i]) != VK_SUCCESS) {
            return false;
        }
    }

    VkAttachmentDescription att{};
    att.format = d.swap_format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rci{};
    rci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rci.attachmentCount = 1;
    rci.pAttachments = &att;
    rci.subpassCount = 1;
    rci.pSubpasses = &sub;
    if (vkCreateRenderPass(d.vk, &rci, nullptr, &d.render_pass) != VK_SUCCESS) return false;

    d.framebuffers.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = d.render_pass;
        fci.attachmentCount = 1;
        fci.pAttachments = &d.swap_views[i];
        fci.width = d.swap_w;
        fci.height = d.swap_h;
        fci.layers = 1;
        if (vkCreateFramebuffer(d.vk, &fci, nullptr, &d.framebuffers[i]) != VK_SUCCESS) {
            return false;
        }
    }

    d.cmds.resize(n);
    VkCommandBufferAllocateInfo aci{};
    aci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    aci.commandPool = d.cmd_pool;
    aci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    aci.commandBufferCount = uint32_t(d.cmds.size());
    if (vkAllocateCommandBuffers(d.vk, &aci, d.cmds.data()) != VK_SUCCESS) return false;

    return true;
}

} // namespace shim

// ---- public API (mng::nvn) --------------------------------------------------

int shim_attach_native_window(void* anative_window, int32_t* out_w, int32_t* out_h) {
    std::lock_guard<std::mutex> lk(shim::g_mu);
    auto& dev = shim::g_dev;
    auto* win = static_cast<ANativeWindow*>(anative_window);
    if (win == nullptr) return -1;
    if (dev.context_ok) {
        if (out_w) *out_w = int32_t(dev.swap_w);
        if (out_h) *out_h = int32_t(dev.swap_h);
        return 0;
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "MacNativeGames-NVN";
    app.apiVersion = VK_API_VERSION_1_1;
    const char* inst_exts[] = {"VK_KHR_android_surface", "VK_KHR_surface"};
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = inst_exts;
    if (vkCreateInstance(&ici, nullptr, &dev.instance) != VK_SUCCESS) return -2;

    VkAndroidSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    sci.window = win;
    if (vkCreateAndroidSurfaceKHR(dev.instance, &sci, nullptr, &dev.surface) != VK_SUCCESS) {
        return -3;
    }

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(dev.instance, &count, nullptr);
    if (count == 0) return -4;
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(dev.instance, &count, devs.data());
    for (auto pd : devs) {
        uint32_t fam = 0;
        vkGetPhysicalDeviceQueueFamilyProperties2(pd, &fam, nullptr);
        std::vector<VkQueueFamilyProperties2> props(fam);
        for (auto& p : props) p.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        vkGetPhysicalDeviceQueueFamilyProperties2(pd, &fam, props.data());
        bool found = false;
        for (uint32_t i = 0; i < fam; ++i) {
            VkBool32 sup = VK_FALSE;
            if ((props[i].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, dev.surface, &sup) ==
                    VK_SUCCESS &&
                sup) {
                dev.physical = pd;
                dev.queue_family = i;
                found = true;
                break;
            }
        }
        if (found) break;
    }
    if (dev.physical == VK_NULL_HANDLE) return -5;

    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = dev.queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    const char* dev_exts[] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dev_exts;
    if (vkCreateDevice(dev.physical, &dci, nullptr, &dev.vk) != VK_SUCCESS) return -6;
    vkGetDeviceQueue(dev.vk, dev.queue_family, 0, &dev.queue);

    VkCommandPoolCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = dev.queue_family;
    if (vkCreateCommandPool(dev.vk, &cpi, nullptr, &dev.cmd_pool) != VK_SUCCESS) return -7;

    VkSemaphoreCreateInfo ssci{};
    ssci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(dev.vk, &ssci, nullptr, &dev.sem_image);
    vkCreateSemaphore(dev.vk, &ssci, nullptr, &dev.sem_render);
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(dev.vk, &fci, nullptr, &dev.submit_fence);

    if (!shim::create_swapchain_resources(dev)) return -8;

    dev.context_ok = true;
    shim::g_counters.device_initialize_ok++;
    if (out_w) *out_w = int32_t(dev.swap_w);
    if (out_h) *out_h = int32_t(dev.swap_h);
    SHLOGI("NVN shim context ready (%ux%u)", dev.swap_w, dev.swap_h);
    return 0;
}

// Defined here; exposed through nvn_shim_internal.h inside namespace shim.
} // namespace mng::nvn

namespace mng::nvn {
namespace shim {

VkPipeline build_vk_pipeline(Shader* sh) {
    auto& dev = g_dev;
    if (!dev.context_ok || sh == nullptr) return VK_NULL_HANDLE;
    if (sh->spirv_vs.empty() || sh->spirv_fs.empty()) return VK_NULL_HANDLE;

    VkPipelineLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(dev.vk, &lci, nullptr, &layout) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    VkShaderModule vs = make_module(dev.vk, sh->spirv_vs.data(), sh->spirv_vs.size());
    VkShaderModule fs = make_module(dev.vk, sh->spirv_fs.data(), sh->spirv_fs.size());
    if (vs == VK_NULL_HANDLE || fs == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &ba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{};
    dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dy;
    pci.layout = layout;
    pci.renderPass = dev.render_pass;
    VkPipeline p = VK_NULL_HANDLE;
    const VkResult rc =
        vkCreateGraphicsPipelines(dev.vk, VK_NULL_HANDLE, 1, &pci, nullptr, &p);
    vkDestroyShaderModule(dev.vk, vs, nullptr);
    vkDestroyShaderModule(dev.vk, fs, nullptr);
    if (rc != VK_SUCCESS) {
        vkDestroyPipelineLayout(dev.vk, layout, nullptr);
        return VK_NULL_HANDLE;
    }
    return p;
}

} // namespace shim
} // namespace mng::nvn (outer)

namespace mng::nvn {

uint32_t shim_texture_count() {
    std::lock_guard<std::mutex> lk(shim::g_mu);
    return uint32_t(shim::g_dev.textures.size());
}

uint32_t shim_shader_count() {
    std::lock_guard<std::mutex> lk(shim::g_mu);
    return uint32_t(shim::g_dev.shaders.size());
}

const ShimCounters& counters() { return shim::g_counters; }
void reset_counters() { shim::g_counters = ShimCounters{}; }

} // namespace mng::nvn (counters)
