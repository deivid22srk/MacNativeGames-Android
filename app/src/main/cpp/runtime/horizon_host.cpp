#include "runtime/horizon_host.h"

#include <chrono>
#include <algorithm>

namespace mng::horizon {

bool ServiceRegistry::register_service(const std::string& name, Handler handler) {
    return services_.emplace(name, std::move(handler)).second;
}

bool ServiceRegistry::dispatch(const std::string& name,
                               const std::vector<uint8_t>& request,
                               std::vector<uint8_t>* reply) const {
    auto it = services_.find(name);
    if (it == services_.end()) return false;
    *reply = it->second(request);
    return true;
}

size_t ServiceRegistry::size() const { return services_.size(); }

std::vector<std::string> ServiceRegistry::names() const {
    std::vector<std::string> out;
    out.reserve(services_.size());
    for (const auto& [name, _] : services_) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

Handler make_time_service() {
    return [](const std::vector<uint8_t>&) -> std::vector<uint8_t> {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const uint64_t seconds =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(now).count());
        std::vector<uint8_t> reply(8);
        for (int i = 0; i < 8; ++i) reply[i] = static_cast<uint8_t>((seconds >> (8 * i)) & 0xff);
        return reply;
    };
}

Handler make_settings_service() {
    return [](const std::vector<uint8_t>&) -> std::vector<uint8_t> {
        // Deterministic placeholder block: [0]=language, [1]=region, [2..3]=refresh*10 (60Hz).
        return std::vector<uint8_t>{0x01, 0x00, 0x3c, 0x00};
    };
}

} // namespace mng::horizon
