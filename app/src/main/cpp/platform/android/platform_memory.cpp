#include "platform/platform_memory.h"

#include <sys/mman.h>
#include <unistd.h>

namespace mng::platform {

size_t page_size() {
    static const size_t ps = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    return ps;
}

static size_t round_up(size_t v, size_t a) {
    return (v + a - 1) / a * a;
}

static void* mmap_hinted(size_t size, uint64_t near_hint, int prot) {
    const size_t ps = page_size();
    const size_t total = round_up(size, ps);
    void* addr = mmap(nullptr, total, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) return nullptr;

    // If a placement constraint was requested and the kernel ignored it, retry
    // with a fixed hint (best effort — the M2 loader will fall back to the
    // upstream "side-by-side hole" layout if this still cannot land < 2^39).
    if (near_hint != 0 && reinterpret_cast<uint64_t>(addr) >= near_hint) {
        munmap(addr, total);
        void* fixed = mmap(reinterpret_cast<void*>(near_hint), total, prot,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (fixed == MAP_FAILED) return nullptr;
        return fixed;
    }
    return addr;
}

void* allocate_guest_code_memory(size_t size, uint64_t near_hint) {
    // RW first; flipped to RX by make_executable() (W^X discipline).
    return mmap_hinted(size, near_hint, PROT_READ | PROT_WRITE);
}

void* allocate_guest_data_memory(size_t size, uint64_t near_hint) {
    return mmap_hinted(size, near_hint, PROT_READ | PROT_WRITE);
}

bool make_executable(void* addr, size_t size) {
    return mprotect(addr, round_up(size, page_size()), PROT_READ | PROT_EXEC) == 0;
}

bool make_writable(void* addr, size_t size) {
    return mprotect(addr, round_up(size, page_size()), PROT_READ | PROT_WRITE) == 0;
}

void free_guest_memory(void* addr, size_t size) {
    if (addr != nullptr) {
        munmap(addr, round_up(size, page_size()));
    }
}

} // namespace mng::platform
