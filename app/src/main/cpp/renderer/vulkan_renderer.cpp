// MacNativeGames-Android — renderer de diagnóstico Vulkan (marco M1). MIT License.
#include "vulkan_renderer.h"

#include <android/log.h>
#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstring>
#include <vector>

#include "shaders.h"

#define LOG_TAG "MNG-Vulkan"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define VK_CHECK(call)                                                          \
    do {                                                                        \
        VkResult _r = (call);                                                   \
        if (_r != VK_SUCCESS) {                                                 \
            LOGE("%s falhou: %s (%d)", #call, string_VkResult_local(_r), _r);   \
            return false;                                                       \
        }                                                                       \
    } while (0)

namespace mng {
namespace {

const char* string_VkResult_local(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "DEVICE_LOST";
        case VK_ERROR_OUT_OF_DATE_KHR: return "OUT_OF_DATE_KHR";
        case VK_SUBOPTIMAL_KHR: return "SUBOPTIMAL_KHR";
        default: return "OUTRO";
    }
}

// pimpl: mantém handles Vulkan fora do header.
struct VkHandles {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule fs = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, 2> cmds{};
    std::array<VkSemaphore, 2> imageAvailable{};
    std::array<VkSemaphore, 2> renderDone{};
    std::array<VkFence, 2> inFlight{};
};

VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, uint32_t words) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = static_cast<size_t>(words) * sizeof(uint32_t);
    ci.pCode = code;
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS) return VK_NULL_HANDLE;
    return mod;
}

} // namespace

bool VulkanRenderer::Start(ANativeWindow* window) {
    if (running_.load()) return true;
    window_ = window;
    ANativeWindow_acquire(window_);
    running_.store(true);
    vk_ = new VkHandles();
    thread_ = std::thread([this] { Run(); });
    return true;
}

void VulkanRenderer::Stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    DestroyAll();
    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
    delete vk_;
    vk_ = nullptr;
}

VulkanRenderer::~VulkanRenderer() { Stop(); }

bool VulkanRenderer::CreateInstance() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "MacNativeGames";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "mng-runtime";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    const char* exts[] = {"VK_KHR_surface", "VK_KHR_android_surface"};
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = exts;
    VK_CHECK(vkCreateInstance(&ci, nullptr, &vk_->instance));

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(vk_->instance, &count, nullptr);
    if (count == 0) {
        LOGE("Nenhum physical device Vulkan");
        return false;
    }
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(vk_->instance, &count, devs.data());
    vk_->physical = devs[0];

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(vk_->physical, &props);
    LOGI("GPU: %s | API %u.%u.%u", props.deviceName,
         VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
         VK_API_VERSION_PATCH(props.apiVersion));
    return true;
}

bool VulkanRenderer::CreateSurfaceAndDevice() {
    VkAndroidSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    sci.window = window_;
    VK_CHECK(vkCreateAndroidSurfaceKHR(vk_->instance, &sci, nullptr, &vk_->surface));

    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk_->physical, &families, nullptr);
    std::vector<VkQueueFamilyProperties> props(families);
    vkGetPhysicalDeviceQueueFamilyProperties(vk_->physical, &families, props.data());

    bool found = false;
    for (uint32_t i = 0; i < families; ++i) {
        if (!(props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(vk_->physical, i, vk_->surface, &present);
        if (present) {
            vk_->queueFamily = i;
            found = true;
            break;
        }
    }
    if (!found) {
        LOGE("Fila com graphics+present não encontrada");
        return false;
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = vk_->queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char* devExts[] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;
    VK_CHECK(vkCreateDevice(vk_->physical, &dci, nullptr, &vk_->device));
    vkGetDeviceQueue(vk_->device, vk_->queueFamily, 0, &vk_->queue);
    return true;
}

bool VulkanRenderer::CreateSwapchainObjects() {
    VkSurfaceCapabilitiesKHR caps{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_->physical, vk_->surface, &caps));

    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk_->physical, vk_->surface, &nf, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(nf == 0 ? 1 : nf);
    if (nf != 0) vkGetPhysicalDeviceSurfaceFormatsKHR(vk_->physical, vk_->surface, &nf, formats.data());
    vk_->swapFormat = VK_FORMAT_R8G8B8A8_UNORM;
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) {
            vk_->swapFormat = f.format;
            break;
        }
    }

    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) {
        ext.width = static_cast<uint32_t>(ANativeWindow_getWidth(window_));
        ext.height = static_cast<uint32_t>(ANativeWindow_getHeight(window_));
        ext.width = std::clamp(ext.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        ext.height = std::clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    vk_->extent = ext;
    lastWidth_ = ext.width;
    lastHeight_ = ext.height;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = vk_->surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = vk_->swapFormat;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = ext;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // garantido por spec
    sci.clipped = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(vk_->device, &sci, nullptr, &vk_->swapchain));

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(vk_->device, vk_->swapchain, &n, nullptr);
    vk_->images.resize(n);
    vkGetSwapchainImagesKHR(vk_->device, vk_->swapchain, &n, vk_->images.data());

    // views
    vk_->views.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = vk_->images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = vk_->swapFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(vk_->device, &vci, nullptr, &vk_->views[i]));
    }

    // render pass
    VkAttachmentDescription att{};
    att.format = vk_->swapFormat;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    VK_CHECK(vkCreateRenderPass(vk_->device, &rpci, nullptr, &vk_->renderPass));

    // framebuffers
    vk_->framebuffers.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = vk_->renderPass;
        fci.attachmentCount = 1;
        fci.pAttachments = &vk_->views[i];
        fci.width = ext.width;
        fci.height = ext.height;
        fci.layers = 1;
        VK_CHECK(vkCreateFramebuffer(vk_->device, &fci, nullptr, &vk_->framebuffers[i]));
    }

    // pipeline
    vk_->vs = CreateShaderModule(vk_->device, kVertSpv, kVertSpv_size);
    vk_->fs = CreateShaderModule(vk_->device, kFragSpv, kFragSpv_size);
    if (vk_->vs == VK_NULL_HANDLE || vk_->fs == VK_NULL_HANDLE) {
        LOGE("Falha ao criar shader modules");
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vk_->vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = vk_->fs;
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
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blendAtt;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

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
    pci.pDynamicState = &dyn;
    pci.layout = vk_->pipelineLayout;
    pci.renderPass = vk_->renderPass;
    pci.subpass = 0;
    VK_CHECK(vkCreateGraphicsPipelines(vk_->device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                       &vk_->pipeline));

    // command pool + buffers
    VkCommandPoolCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = vk_->queueFamily;
    VK_CHECK(vkCreateCommandPool(vk_->device, &cpi, nullptr, &vk_->cmdPool));
    VkCommandBufferAllocateInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbi.commandPool = vk_->cmdPool;
    cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbi.commandBufferCount = static_cast<uint32_t>(vk_->cmds.size());
    VK_CHECK(vkAllocateCommandBuffers(vk_->device, &cbi, vk_->cmds.data()));

    // sync
    VkSemaphoreCreateInfo sci2{};
    sci2.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i = 0; i < vk_->imageAvailable.size(); ++i) {
        VK_CHECK(vkCreateSemaphore(vk_->device, &sci2, nullptr, &vk_->imageAvailable[i]));
        VK_CHECK(vkCreateSemaphore(vk_->device, &sci2, nullptr, &vk_->renderDone[i]));
        VK_CHECK(vkCreateFence(vk_->device, &fci, nullptr, &vk_->inFlight[i]));
    }
    return true;
}

void VulkanRenderer::DestroySwapchainObjects() {
    VkDevice d = vk_->device;
    for (auto f : vk_->inFlight) if (f) vkDestroyFence(d, f, nullptr);
    for (auto s : vk_->renderDone) if (s) vkDestroySemaphore(d, s, nullptr);
    for (auto s : vk_->imageAvailable) if (s) vkDestroySemaphore(d, s, nullptr);
    vk_->inFlight.fill(VK_NULL_HANDLE);
    vk_->renderDone.fill(VK_NULL_HANDLE);
    vk_->imageAvailable.fill(VK_NULL_HANDLE);
    if (vk_->cmdPool) vkDestroyCommandPool(d, vk_->cmdPool, nullptr);
    vk_->cmdPool = VK_NULL_HANDLE;
    if (vk_->pipeline) vkDestroyPipeline(d, vk_->pipeline, nullptr);
    vk_->pipeline = VK_NULL_HANDLE;
    if (vk_->pipelineLayout) vkDestroyPipelineLayout(d, vk_->pipelineLayout, nullptr);
    vk_->pipelineLayout = VK_NULL_HANDLE;
    if (vk_->vs) vkDestroyShaderModule(d, vk_->vs, nullptr);
    if (vk_->fs) vkDestroyShaderModule(d, vk_->fs, nullptr);
    vk_->vs = vk_->fs = VK_NULL_HANDLE;
    for (auto fb : vk_->framebuffers) if (fb) vkDestroyFramebuffer(d, fb, nullptr);
    vk_->framebuffers.clear();
    if (vk_->renderPass) vkDestroyRenderPass(d, vk_->renderPass, nullptr);
    vk_->renderPass = VK_NULL_HANDLE;
    for (auto v : vk_->views) if (v) vkDestroyImageView(d, v, nullptr);
    vk_->views.clear();
    vk_->images.clear();
    if (vk_->swapchain) vkDestroySwapchainKHR(d, vk_->swapchain, nullptr);
    vk_->swapchain = VK_NULL_HANDLE;
}

void VulkanRenderer::DestroyAll() {
    if (!vk_ || vk_->device == VK_NULL_HANDLE) {
        if (vk_ && vk_->instance) vkDestroyInstance(vk_->instance, nullptr);
        return;
    }
    vkDeviceWaitIdle(vk_->device);
    DestroySwapchainObjects();
    vkDestroyDevice(vk_->device, nullptr);
    vk_->device = VK_NULL_HANDLE;
    if (vk_->surface) vkDestroySurfaceKHR(vk_->instance, vk_->surface, nullptr);
    vk_->surface = VK_NULL_HANDLE;
    vkDestroyInstance(vk_->instance, nullptr);
    vk_->instance = VK_NULL_HANDLE;
}

void VulkanRenderer::Run() {
    if (!CreateInstance()) return;
    if (!CreateSurfaceAndDevice()) return;

    // Pipeline layout sem descritores.
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(vk_->device, &plci, nullptr, &vk_->pipelineLayout) != VK_SUCCESS) {
        LOGE("pipeline layout falhou");
        return;
    }

    if (!CreateSwapchainObjects()) return;
    LOGI("Swapchain %ux%u pronto; iniciando loop", vk_->extent.width, vk_->extent.height);

    uint32_t frameIdx = 0;
    while (running_.load()) {
        // recria swapchain quando a janela muda de tamanho
        const uint32_t w = static_cast<uint32_t>(ANativeWindow_getWidth(window_));
        const uint32_t h = static_cast<uint32_t>(ANativeWindow_getHeight(window_));
        if (w != lastWidth_ || h != lastHeight_) {
            vkDeviceWaitIdle(vk_->device);
            DestroySwapchainObjects();
            if (!CreateSwapchainObjects()) {
                LOGE("Falha ao recriar swapchain");
                return;
            }
        }

        uint32_t img = 0;
        VkResult r = vkAcquireNextImageKHR(vk_->device, vk_->swapchain, UINT64_MAX,
                                           vk_->imageAvailable[frameIdx], VK_NULL_HANDLE, &img);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDeviceWaitIdle(vk_->device);
            DestroySwapchainObjects();
            if (!CreateSwapchainObjects()) return;
            continue;
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            LOGE("acquire falhou: %d", r);
            return;
        }

        vkWaitForFences(vk_->device, 1, &vk_->inFlight[frameIdx], VK_TRUE, UINT64_MAX);
        vkResetFences(vk_->device, 1, &vk_->inFlight[frameIdx]);

        VkCommandBuffer cmd = vk_->cmds[frameIdx];
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &bi);

        // cor de limpeza animada (azul escuro pulsante)
        float t = (frame_ % 600) / 600.0f;
        VkClearColorValue clear{};
        clear.float32[0] = 0.05f + 0.03f * t;
        clear.float32[1] = 0.07f;
        clear.float32[2] = 0.12f + 0.08f * (1.0f - t);
        clear.float32[3] = 1.0f;

        VkRenderPassBeginInfo rbi{};
        rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rbi.renderPass = vk_->renderPass;
        rbi.framebuffer = vk_->framebuffers[img];
        rbi.renderArea.extent = vk_->extent;
        rbi.clearValueCount = 1;
        rbi.pClearValues = reinterpret_cast<VkClearValue*>(&clear);
        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{0, 0,
                            static_cast<float>(vk_->extent.width),
                            static_cast<float>(vk_->extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, vk_->extent};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_->pipeline);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &vk_->imageAvailable[frameIdx];
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &vk_->renderDone[frameIdx];
        r = vkQueueSubmit(vk_->queue, 1, &si, vk_->inFlight[frameIdx]);
        if (r != VK_SUCCESS) {
            LOGE("submit falhou: %d", r);
            return;
        }

        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &vk_->renderDone[frameIdx];
        pi.swapchainCount = 1;
        pi.pSwapchains = &vk_->swapchain;
        pi.pImageIndices = &img;
        r = vkQueuePresentKHR(vk_->queue, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
            vkDeviceWaitIdle(vk_->device);
            DestroySwapchainObjects();
            if (!CreateSwapchainObjects()) return;
        }

        frameIdx = (frameIdx + 1) % vk_->cmds.size();
        ++frame_;
        if (frame_ == 1) LOGI("Primeiro quadro apresentado (M1 OK)");
        if (frame_ % 300 == 0) LOGI("quadros=%u", frame_);
    }
    LOGI("Loop encerrado após %u quadros", frame_);
}

} // namespace mng
