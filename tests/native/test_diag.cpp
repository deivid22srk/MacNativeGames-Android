// M5 tests — diagnostics ring + memory budget math.
#include <cstdio>

#include "diag/diag.h"

using mng::diag::budget_from_meminfo;
using mng::diag::dump;
using mng::diag::event;

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

int main() {
    // Budget math: 8 GiB device → capped at 4 GiB addressable.
    {
        const auto b = budget_from_meminfo(8192, 1024);
        CHECK(b.budget_mib == 4096, "8GiB device caps at 4GiB budget");
        CHECK(b.device_mem_total_mib == 8192, "mem total echoed");
    }
    // Budget math: 3 GiB device minus reserve → 2 GiB.
    {
        const auto b = budget_from_meminfo(3072, 1024);
        CHECK(b.budget_mib == 2048, "small device uses mem-reserve");
    }
    // Degenerate: reserve >= total → 0, never negative.
    {
        const auto b = budget_from_meminfo(512, 1024);
        CHECK(b.budget_mib == 0, "no underflow when reserve exceeds total");
    }

    // Ring: events appear oldest-first; cap does not crash.
    event("t", "one");
    event("t", "two");
    const std::string d = dump();
    CHECK(d.find("one") < d.find("two"), "ring order is oldest first");
    for (int i = 0; i < 300; ++i) {
        event("t", "filler " + std::to_string(i));
    }
    CHECK(dump().find("one") == std::string::npos, "oldest evicted after cap");
    CHECK(dump().find("filler 299") != std::string::npos, "newest retained");

    if (failures == 0) std::printf("diag tests PASSED\n");
    return failures == 0 ? 0 : 1;
}
