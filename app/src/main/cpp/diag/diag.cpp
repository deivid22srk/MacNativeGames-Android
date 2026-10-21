#include "diag/diag.h"

#include <cstdio>
#include <mutex>
#include <vector>

namespace mng::diag {
namespace {

constexpr size_t kRingSize = 256;

struct Entry {
    std::string tag;
    std::string message;
};

std::mutex g_mu;
std::vector<Entry> g_ring;
size_t g_cursor = 0; // next write position once the ring is full

} // namespace

void event(const char* tag, const std::string& message) {
    std::lock_guard<std::mutex> lk(g_mu);
    const Entry e{tag ? tag : "?", message};
    if (g_ring.size() < kRingSize) {
        g_ring.push_back(e);
    } else {
        g_ring[g_cursor] = e;
        g_cursor = (g_cursor + 1) % kRingSize;
    }
}

std::string dump() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::string out;
    for (const auto& e : g_ring) {
        out += "[";
        out += e.tag;
        out += "] ";
        out += e.message;
        out += "\n";
    }
    return out;
}

MemoryBudget budget_from_meminfo(uint64_t mem_total_mib, uint64_t reserve_mib) {
    MemoryBudget b;
    b.device_mem_total_mib = mem_total_mib;
    b.reserve_for_system_mib = reserve_mib;
    const uint64_t usable = mem_total_mib > reserve_mib ? mem_total_mib - reserve_mib : 0;
    b.budget_mib = usable < b.game_addressable_mib ? usable : b.game_addressable_mib;
    return b;
}

MemoryBudget budget_from_device() {
    FILE* f = fopen("/proc/meminfo", "r");
    if (f == nullptr) return MemoryBudget{};
    char line[256];
    uint64_t total_kb = 0;
    while (fgets(line, sizeof(line), f) != nullptr) {
        if (sscanf(line, "MemTotal: %lu kB", &total_kb) == 1) break;
    }
    fclose(f);
    if (total_kb == 0) return MemoryBudget{};
    constexpr uint64_t kReserveMib = 1024; // OS + app + driver headroom
    return budget_from_meminfo(total_kb / 1024, kReserveMib);
}

} // namespace mng::diag
