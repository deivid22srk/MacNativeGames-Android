// Horizon (Switch OS) system service host for Android — M2 scaffolding.
//
// The game talks to Horizon through syscalls (rewritten by the NSO rewriter into
// jumps here) and IPC. Upstream answers those calls from games/totk/runtime/*.c
// on macOS. On Android the service *contracts* are identical; the transport
// (how a rewritten `svc` lands in the host) is the Bionic platform layer.
//
// This file provides the service registry and two genuinely implemented
// services (time and settings) as the first testable slice. Threads, FS,
// audio, HID, and the rest land behind the same registry in later milestones.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mng::horizon {

// A service receives an opaque request payload and produces a reply payload.
// Wire formats are Milestone-2+ work; for now the registry is exercised with
// synthetic fixtures on the host to pin the dispatch semantics.
using Handler = std::function<std::vector<uint8_t>(const std::vector<uint8_t>& request)>;

class ServiceRegistry {
public:
    // Registers a service under its Horizon name (e.g. "time:u", "set:sys").
    // Returns false if the name is already registered.
    bool register_service(const std::string& name, Handler handler);

    // Dispatches a request to a registered service. Returns false if absent.
    bool dispatch(const std::string& name, const std::vector<uint8_t>& request,
                  std::vector<uint8_t>* reply) const;

    size_t size() const;
    std::vector<std::string> names() const; // sorted; used by hostRegistryReport()

private:
    std::map<std::string, Handler> services_;
};

// ---- Implemented (unit-tested) services ---------------------------------

// "time:u" — replies with steady/real clock seconds as 8-byte LE payload.
Handler make_time_service();

// "set:sys" — replies with a fixed settings block describing the Android
// frontend (language id, region, output refresh). Purely deterministic.
Handler make_settings_service();

} // namespace mng::horizon
