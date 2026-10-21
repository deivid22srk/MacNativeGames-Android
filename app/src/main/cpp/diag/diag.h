// M5 — diagnostics ring + memory budget (host-testable parts isolated).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mng::diag {

// Records an event into a fixed-size in-memory ring (survives nothing — it is
// a diagnostic snapshot, not a log sink). Thread-safe.
void event(const char* tag, const std::string& message);

// Dumps the ring, oldest first, one line per event. Never contains user file
// names or key material — callers must pass sanitized strings.
std::string dump();

// M5 — device memory budget. Reads /proc/meminfo MemTotal and derives:
//   * game_addressable = 4 GiB (Switch VA space the guest expects)
//   * budget           = min(4 GiB, mem_total - reserve_for_system)
// Values are rounded down to MiB. Pure function of the input so tests can
// verify the math without /proc.
struct MemoryBudget {
    uint64_t device_mem_total_mib = 0;
    uint64_t reserve_for_system_mib = 0;
    uint64_t budget_mib = 0;
    uint64_t game_addressable_mib = 4096;
};
MemoryBudget budget_from_meminfo(uint64_t mem_total_mib, uint64_t reserve_mib);
MemoryBudget budget_from_device(); // reads /proc/meminfo; 0s on read failure

} // namespace mng::diag
