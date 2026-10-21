// MacNativeGames-Android — implementação Android da camada de plataforma. MIT License.
//
// W^X no Android: apps JIT usam mapeamento duplo (um fd anônimo mapeado duas vezes,
// uma RW e outra RX). Isso evita RWX real e é compatível com as políticas SELinux e
// com o endurecimento recente (Android 15/16) contra RWX em apps alvo API 35+.
// O loader do projeto original usa o mesmo princípio em macOS (MAP_JIT + W^X toggle).
#include "platform/platform.h"

#include <android/log.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace mng {

namespace {
size_t PageSize() { return static_cast<size_t>(sysconf(_SC_PAGESIZE)); }
size_t AlignUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }
} // namespace

bool ExecutableMemory::Allocate(size_t size) {
    Free();
    size_ = AlignUp(size, PageSize());
#ifdef SYS_memfd_create
    fd_ = static_cast<int>(syscall(SYS_memfd_create, "mng-exec", 0));
#else
    fd_ = -1;
#endif
    if (fd_ < 0) {
        // Fallback documentado: memfd_create indisponível (não deveria ocorrer em API 29+).
        PlatformLogError("MNG-Platform", "memfd_create falhou; W^X dual-map indisponível");
        return false;
    }
    if (ftruncate(fd_, static_cast<off_t>(size_)) != 0) {
        PlatformLogError("MNG-Platform", "ftruncate falhou");
        Free();
        return false;
    }
    rw_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    rx_ = mmap(nullptr, size_, PROT_READ | PROT_EXEC, MAP_SHARED, fd_, 0);
    if (rw_ == MAP_FAILED || rx_ == MAP_FAILED) {
        PlatformLogError("MNG-Platform", "mapeamento dual falhou (SELinux?)");
        Free();
        return false;
    }
    return true;
}

bool ExecutableMemory::SealExecutable() {
    // No modelo dual-map a área RX já é executável e a RW não é;
    // nada a fazer além de validar. Mantido para simetria com macOS.
    return rw_ != nullptr && rx_ != nullptr;
}

void ExecutableMemory::Free() {
    if (rw_ && rw_ != MAP_FAILED) munmap(rw_, size_);
    if (rx_ && rx_ != MAP_FAILED) munmap(rx_, size_);
    if (fd_ >= 0) close(fd_);
    rw_ = rx_ = nullptr;
    size_ = 0;
    fd_ = -1;
}

void PlatformLogInfo(const char* tag, const char* message) {
    __android_log_print(ANDROID_LOG_INFO, tag, "%s", message);
}

void PlatformLogError(const char* tag, const char* message) {
    __android_log_print(ANDROID_LOG_ERROR, tag, "%s", message);
}

} // namespace mng
