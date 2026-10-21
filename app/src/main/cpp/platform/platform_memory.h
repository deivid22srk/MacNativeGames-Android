// Platform memory primitives for the guest runtime (Android side).
//
// Upstream (macOS) uses MAP_JIT + W^X entitlements for rewritten game code.
// On Android, anonymous executable memory is created with mmap(PROT_WRITE)
// followed by mprotect(PROT_READ|PROT_EXEC). The SELinux untrusted_app domain
// permits execmem on anonymous mappings (JITs such as V8/Mono rely on this);
// executable memory backed by app-writable *files* is what targetSdk 29+ forbids,
// and we never do that.
#pragma once

#include <cstddef>
#include <cstdint>

namespace mng::platform {

// Allocates `size` bytes of anonymous memory whose permissions can later be
// flipped to executable. Returns nullptr on failure. `near_hint` optionally
// biases the kernel to place the mapping below a given address (guest layout
// requires everything < 2^39; see docs/android-port-analysis.md).
void* allocate_guest_code_memory(size_t size, uint64_t near_hint = 0);

// Allocates anonymous RW memory with the same hint discipline.
void* allocate_guest_data_memory(size_t size, uint64_t near_hint = 0);

// Flips allocated memory RW -> RX (after writing rewritten code).
bool make_executable(void* addr, size_t size);

// Restores RW (for further patching).
bool make_writable(void* addr, size_t size);

// Frees either allocation type.
void free_guest_memory(void* addr, size_t size);

// Page size of the running device (4 KB or 16 KB).
size_t page_size();

} // namespace mng::platform
