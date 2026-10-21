#include "gfx/vulkan_renderer.h"

#include "gfx/embedded_shaders.h"

#include <android/log.h>
// Must be defined before vulkan.h to expose the Android WSI entry points
// (VkAndroidSurfaceCreateInfoKHR / vkCreateAndroidSurfaceKHR).
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>

#include <cstring>
#include <optional>

#define LOG_TAG "mng-gfx"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace mng::gfx {

struct VulkanRenderer::Impl {
    ANativeWindow* window = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t frames = 0;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t graphics_family = 0;
    uint32_t present_family = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swap_images;
    std::vector<VkImageView> swap_views;
    std::vector<VkFramebuffer> framebuffers;
    VkFormat swap_format = VK_FORMAT_UNDEFINED;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmd_buffers;
    VkSemaphore sem_image = VK_NULL_HANDLE;
    VkSemaphore sem_render = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
};

static const char* kValidationNote = "M1";

// ---------- helpers ---------------------------------------------------------

static VkShaderModule create_module(VkDevice device, const uint32_t* words,
                                    uint32_t word_count) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.pCode = words;
    ci.codeSize = static_cast<size_t>(word_count) * sizeof(uint32_t);
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return module;
}

// ---------- init ------------------------------------------------------------

int VulkanRenderer::init(ANativeWindow* window) {
    impl_ = new Impl();
    impl_->window = window;
    impl_->width = static_cast<uint32_t>(ANativeWindow_getWidth(window));
    impl_->height = static_cast<uint32_t>(ANativeWindow_getHeight(window));

    if (!create_instance()) return 1;
    if (!create_surface()) return 2;
    if (!pick_device()) return 3;
    if (!create_swapchain()) return 4;
    if (!create_render_pass()) return 5;
    if (!create_pipeline()) return 6;
    if (!create_sync()) return 7;

    LOGI("M1 Vulkan renderer initialized (%ux%u) — %s", impl_->width, impl_->height,
         kValidationNote);
    return 0;
}

bool VulkanRenderer::create_instance() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "MacNativeGames-Android";
    app.apiVersion = VK_API_VERSION_1_1;

    const char* instance_exts[] = {"VK_KHR_android_surface", "VK_KHR_surface"};
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = instance_exts;

    return vkCreateInstance(&ci, nullptr, &impl_->instance) == VK_SUCCESS;
}

bool VulkanRenderer::create_surface() {
    VkAndroidSurfaceCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    ci.window = impl_->window;
    return vkCreateAndroidSurfaceKHR(impl_->instance, &ci, nullptr, &impl_->surface) ==
           VK_SUCCESS;
}

bool VulkanRenderer::pick_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(impl_->instance, &count, nullptr);
    if (count == 0) return false;
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(impl_->instance, &count, devices.data());

    for (auto pd : devices) {
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties2(pd, &families, nullptr);
        std::vector<VkQueueFamilyProperties2> props(families);
        for (auto& p : props) p.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        vkGetPhysicalDeviceQueueFamilyProperties2(pd, &families, props.data());

        std::optional<uint32_t> gfx, present;
        for (uint32_t i = 0; i < families; ++i) {
            if (props[i].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                if (!gfx) gfx = i;
                VkBool32 supported = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, impl_->surface, &supported);
                if (supported && !present) present = i;
            }
        }
        if (gfx && present) {
            impl_->physical = pd;
            impl_->graphics_family = *gfx;
            impl_->present_family = *present;
            break;
        }
    }
    if (impl_->physical == VK_NULL_HANDLE) return false;

    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci[2]{};
    uint32_t qci_count = 1;
    qci[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci[0].queueFamilyIndex = impl_->graphics_family;
    qci[0].queueCount = 1;
    qci[0].pQueuePriorities = &prio;
    if (impl_->present_family != impl_->graphics_family) {
        qci[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci[1].queueFamilyIndex = impl_->present_family;
        qci[1].queueCount = 1;
        qci[1].pQueuePriorities = &prio;
        qci_count = 2;
    }

    const char* dev_exts[] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = qci_count;
    dci.pQueueCreateInfos = qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dev_exts;

    if (vkCreateDevice(impl_->physical, &dci, nullptr, &impl_->device) != VK_SUCCESS) {
        return false;
    }
    vkGetDeviceQueue(impl_->device, impl_->graphics_family, 0, &impl_->queue);
    return true;
}

bool VulkanRenderer::create_swapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(impl_->physical, impl_->surface, &caps);

    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(impl_->physical, impl_->surface, &fmt_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(impl_->physical, impl_->surface, &fmt_count,
                                         formats.data());
    // Prefer UNORM swapchain (sRGB conversion belongs to the future NVN layer).
    impl_->swap_format = VK_FORMAT_R8G8B8A8_UNORM;
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM) {
            impl_->swap_format = f.format;
            break;
        }
    }
    if (impl_->swap_format != VK_FORMAT_R8G8B8A8_UNORM && !formats.empty()) {
        impl_->swap_format = formats[0].format;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    uint32_t families[] = {impl_->graphics_family, impl_->present_family};
    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = impl_->surface;
    sci.minImageCount = image_count;
    sci.imageFormat = impl_->swap_format;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = caps.currentExtent;
    impl_->width = caps.currentExtent.width;
    impl_->height = caps.currentExtent.height;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (impl_->graphics_family == impl_->present_family) {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    } else {
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = families;
    }
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // always supported
    sci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(impl_->device, &sci, nullptr, &impl_->swapchain) != VK_SUCCESS) {
        return false;
    }

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(impl_->device, impl_->swapchain, &n, nullptr);
    impl_->swap_images.resize(n);
    vkGetSwapchainImagesKHR(impl_->device, impl_->swapchain, &n, impl_->swap_images.data());

    impl_->swap_views.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = impl_->swap_images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = impl_->swap_format;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(impl_->device, &vci, nullptr, &impl_->swap_views[i]) !=
            VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::create_render_pass() {
    VkAttachmentDescription color{};
    color.format = impl_->swap_format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;

    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &color;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;

    if (vkCreateRenderPass(impl_->device, &ci, nullptr, &impl_->render_pass) != VK_SUCCESS) {
        return false;
    }

    impl_->framebuffers.resize(impl_->swap_views.size());
    for (size_t i = 0; i < impl_->swap_views.size(); ++i) {
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = impl_->render_pass;
        fci.attachmentCount = 1;
        fci.pAttachments = &impl_->swap_views[i];
        fci.width = impl_->width;
        fci.height = impl_->height;
        fci.layers = 1;
        if (vkCreateFramebuffer(impl_->device, &fci, nullptr, &impl_->framebuffers[i]) !=
            VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::create_pipeline() {
    VkShaderModule vs = create_module(impl_->device, kVertexSpirv, kVertexSpirvSize);
    VkShaderModule fs = create_module(impl_->device, kFragmentSpirv, kFragmentSpirvSize);
    if (vs == VK_NULL_HANDLE || fs == VK_NULL_HANDLE) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_att;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dyn_states;

    VkPipelineLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(impl_->device, &lci, nullptr, &impl_->pipeline_layout) !=
        VK_SUCCESS) {
        return false;
    }

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vertex_input;
    pci.pInputAssemblyState = &input_assembly;
    pci.pViewportState = &viewport;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &msaa;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dynamic;
    pci.layout = impl_->pipeline_layout;
    pci.renderPass = impl_->render_pass;

    const VkResult rc = vkCreateGraphicsPipelines(impl_->device, VK_NULL_HANDLE, 1, &pci,
                                                  nullptr, &impl_->pipeline);
    vkDestroyShaderModule(impl_->device, vs, nullptr);
    vkDestroyShaderModule(impl_->device, fs, nullptr);
    return rc == VK_SUCCESS;
}

bool VulkanRenderer::create_sync() {
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = impl_->graphics_family;
    if (vkCreateCommandPool(impl_->device, &pci, nullptr, &impl_->cmd_pool) != VK_SUCCESS) {
        return false;
    }
    impl_->cmd_buffers.resize(impl_->framebuffers.size());
    VkCommandBufferAllocateInfo aci{};
    aci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    aci.commandPool = impl_->cmd_pool;
    aci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    aci.commandBufferCount = static_cast<uint32_t>(impl_->cmd_buffers.size());
    if (vkAllocateCommandBuffers(impl_->device, &aci, impl_->cmd_buffers.data()) !=
        VK_SUCCESS) {
        return false;
    }

    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(impl_->device, &sci, nullptr, &impl_->sem_image) != VK_SUCCESS ||
        vkCreateSemaphore(impl_->device, &sci, nullptr, &impl_->sem_render) != VK_SUCCESS) {
        return false;
    }
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    return vkCreateFence(impl_->device, &fci, nullptr, &impl_->fence) == VK_SUCCESS;
}

// ---------- frame -----------------------------------------------------------

int VulkanRenderer::render_frame() {
    if (impl_ == nullptr) return -1;

    uint32_t image_index = 0;
    VkResult rc = vkAcquireNextImageKHR(impl_->device, impl_->swapchain, UINT64_MAX,
                                        impl_->sem_image, VK_NULL_HANDLE, &image_index);
    if (rc == VK_ERROR_OUT_OF_DATE_KHR || rc == VK_ERROR_SURFACE_LOST_KHR) {
        resize(impl_->width, impl_->height);
        return static_cast<int>(impl_->frames);
    }
    if (rc != VK_SUCCESS && rc != VK_SUBOPTIMAL_KHR) {
        return static_cast<int>(impl_->frames); // e.g. timeout; retry next frame
    }
    const bool suboptimal = (rc == VK_SUBOPTIMAL_KHR);

    VkCommandBuffer cmd = impl_->cmd_buffers[image_index];
    vkWaitForFences(impl_->device, 1, &impl_->fence, VK_TRUE, UINT64_MAX);
    vkResetFences(impl_->device, 1, &impl_->fence);
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue clear{{{0.06f, 0.07f, 0.09f, 1.0f}}};
    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = impl_->render_pass;
    rpi.framebuffer = impl_->framebuffers[image_index];
    rpi.renderArea.extent = {impl_->width, impl_->height};
    rpi.clearValueCount = 1;
    rpi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(impl_->width),
                        static_cast<float>(impl_->height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {impl_->width, impl_->height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &impl_->sem_image;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &impl_->sem_render;
    vkQueueSubmit(impl_->queue, 1, &si, impl_->fence);

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &impl_->sem_render;
    pi.swapchainCount = 1;
    pi.pSwapchains = &impl_->swapchain;
    pi.pImageIndices = &image_index;
    const VkResult prc = vkQueuePresentKHR(impl_->queue, &pi);
    if (prc == VK_ERROR_OUT_OF_DATE_KHR || prc == VK_SUBOPTIMAL_KHR || suboptimal) {
        resize(impl_->width, impl_->height);
    }

    return static_cast<int>(++impl_->frames);
}

int VulkanRenderer::resize(uint32_t width, uint32_t height) {
    if (impl_ == nullptr) return -1;
    vkDeviceWaitIdle(impl_->device);
    destroy_swapchain();

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(impl_->physical, impl_->surface, &caps);
    (void)width;
    (void)height;
    if (!create_swapchain() || !create_render_pass()) return 1;

    // Swapchain image count may differ after recreation; command buffers are
    // indexed by image index, so reallocate to match the new count.
    if (impl_->cmd_buffers.size() != impl_->framebuffers.size()) {
        impl_->cmd_buffers.clear();
        VkCommandBufferAllocateInfo aci{};
        aci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        aci.commandPool = impl_->cmd_pool;
        aci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        aci.commandBufferCount = static_cast<uint32_t>(impl_->framebuffers.size());
        impl_->cmd_buffers.resize(impl_->framebuffers.size());
        if (vkAllocateCommandBuffers(impl_->device, &aci, impl_->cmd_buffers.data()) !=
            VK_SUCCESS) {
            return 2;
        }
    }

    LOGI("swapchain resized to %ux%u", impl_->width, impl_->height);
    return 0;
}

void VulkanRenderer::destroy_swapchain() {
    for (auto fb : impl_->framebuffers) vkDestroyFramebuffer(impl_->device, fb, nullptr);
    impl_->framebuffers.clear();
    for (auto v : impl_->swap_views) vkDestroyImageView(impl_->device, v, nullptr);
    impl_->swap_views.clear();
    impl_->swap_images.clear();
    if (impl_->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(impl_->device, impl_->swapchain, nullptr);
        impl_->swapchain = VK_NULL_HANDLE;
    }
    if (impl_->render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(impl_->device, impl_->render_pass, nullptr);
        impl_->render_pass = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::shutdown() {
    if (impl_ == nullptr) return;
    vkDeviceWaitIdle(impl_->device);
    destroy_swapchain();
    if (impl_->pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(impl_->device, impl_->pipeline, nullptr);
    if (impl_->pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(impl_->device, impl_->pipeline_layout, nullptr);
    if (impl_->cmd_pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(impl_->device, impl_->cmd_pool, nullptr);
    if (impl_->sem_image != VK_NULL_HANDLE)
        vkDestroySemaphore(impl_->device, impl_->sem_image, nullptr);
    if (impl_->sem_render != VK_NULL_HANDLE)
        vkDestroySemaphore(impl_->device, impl_->sem_render, nullptr);
    if (impl_->fence != VK_NULL_HANDLE) vkDestroyFence(impl_->device, impl_->fence, nullptr);
    if (impl_->device != VK_NULL_HANDLE) vkDestroyDevice(impl_->device, nullptr);
    if (impl_->surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(impl_->instance, impl_->surface, nullptr);
    if (impl_->instance != VK_NULL_HANDLE) vkDestroyInstance(impl_->instance, nullptr);
    delete impl_;
    impl_ = nullptr;
    LOGI("M1 renderer shut down");
}

} // namespace mng::gfx
