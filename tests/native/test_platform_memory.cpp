// M2 tests — platform memory primitives (W^X discipline) on the POSIX host.
#include <cstdio>
#include <cstring>

#include "platform/platform_memory.h"

using mng::platform::allocate_guest_code_memory;
using mng::platform::allocate_guest_data_memory;
using mng::platform::free_guest_memory;
using mng::platform::make_executable;
using mng::platform::make_writable;
using mng::platform::page_size;

static int failures = 0;
#define CHECK(cond, msg)                                     \
    do {                                                     \
        if (!(cond)) {                                       \
            std::printf("FAIL: %s\n", msg);                  \
            ++failures;                                      \
        } else {                                             \
            std::printf("ok: %s\n", msg);                    \
        }                                                    \
    } while (0)

static void test_wx_cycle() {
    const size_t ps = page_size();
    CHECK(ps == 4096 || ps == 16384, "page size is 4K or 16K");

    auto* mem = allocate_guest_code_memory(ps * 2);
    CHECK(mem != nullptr, "code memory allocated");
    if (mem != nullptr) {
        std::memset(mem, 0x90, ps); // write while RW
        CHECK(make_executable(mem, ps), "flip to RX");
        CHECK(make_writable(mem, ps), "flip back to RW");
        free_guest_memory(mem, ps);
    }
}

static void test_hint_below_2pow39() {
    // The guest layout requires everything < 2^39. Ask for a hint below it and
    // verify the mapping landed under the threshold (best-effort on the host).
    const uint64_t k39 = 1ull << 39;
    auto* mem = allocate_guest_data_memory(4096, k39 - (64ull << 20));
    CHECK(mem != nullptr, "hinted data memory allocated");
    if (mem != nullptr) {
        const auto addr = reinterpret_cast<uint64_t>(mem);
        CHECK(addr < k39, "mapping below 2^39");
        free_guest_memory(mem, 4096);
    }
}

int main() {
    test_wx_cycle();
    test_hint_below_2pow39();
    if (failures == 0) {
        std::printf("test_platform_memory: ALL PASS\n");
        return 0;
    }
    std::printf("test_platform_memory: %d FAILURES\n", failures);
    return 1;
}
