// MacNativeGames-Android — renderer de diagnóstico Vulkan (marco M1).
// Caminho gráfico mínimo: instância, swapchain, pipeline e triângulo.
// NÃO é a camada NVN→Vulkan completa (M3); existe para validar o encanamento
// gráfico em device real e servir de esqueleto para o backend NVN.
#pragma once

#include <android/native_window.h>
#include <atomic>
#include <cstdint>
#include <thread>

namespace mng {

class VulkanRenderer {
public:
    // Inicia o loop de renderização em uma thread própria.
    // `window` é retido (ANativeWindow_acquire); `Start` não bloqueia.
    bool Start(ANativeWindow* window);

    // Solicita parada e aguarda a thread (bloqueia no máximo ~1 s).
    void Stop();

    ~VulkanRenderer();

private:
    void Run();
    bool CreateInstance();
    bool CreateSurfaceAndDevice();
    bool CreateSwapchainObjects();
    void DestroySwapchainObjects();
    void DestroyAll();

    ANativeWindow* window_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};

    // Vulkan
    struct VkHandles;
    VkHandles* vk_ = nullptr; // pimpl para manter o header leve

    uint32_t frame_ = 0;
    uint32_t lastWidth_ = 0;
    uint32_t lastHeight_ = 0;
};

} // namespace mng
