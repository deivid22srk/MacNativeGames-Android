// MacNativeGames-Android — camada de plataforma (interfaces portáveis).
// O código do host original usa primitivas de macOS (MAP_JIT, mach_vm); aqui
// definimos o contrato que cada SO alvo implementa. MIT License.
#pragma once

#include <cstddef>
#include <cstdint>

namespace mng {

/// Memória executável (W^X). No Android 10+ há restrições crescentes a RWX;
/// a implementação Android usa mapeamento duplo via memfd_create:
///   mapa RW  (escrita do código reescrito)
///   mapa RX  (execução), ambos apontando para o mesmo arquivo em memória.
/// Em macOS a referência é MAP_JIT + pthread_jit_write_protect_np.
class ExecutableMemory {
public:
    /// Reserva `size` bytes (arredondados à página). Retorna false em falha.
    bool Allocate(size_t size);
    /// Aplica RX na área executável (após terminar de escrever).
    bool SealExecutable();
    /// Libera tudo.
    void Free();

    void* Writable() const { return rw_; }   // usado para escrever bytes
    void* Executable() const { return rx_; } // usado para executar
    size_t Size() const { return size_; }

private:
    void* rw_ = nullptr;
    void* rx_ = nullptr;
    size_t size_ = 0;
    int fd_ = -1;
};

/// Log da plataforma (no Android: __android_log_print; em desktop: stderr).
void PlatformLogInfo(const char* tag, const char* message);
void PlatformLogError(const char* tag, const char* message);

} // namespace mng
