// M2 tests — Horizon host service registry with implemented services.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "runtime/horizon_host.h"

using mng::horizon::make_settings_service;
using mng::horizon::make_time_service;
using mng::horizon::ServiceRegistry;

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

static void test_registry_basics() {
    ServiceRegistry reg;
    CHECK(reg.register_service("time:u", make_time_service()), "register time:u");
    CHECK(!reg.register_service("time:u", make_time_service()), "duplicate rejected");
    CHECK(reg.size() == 1, "size 1 after duplicate");

    std::vector<uint8_t> reply;
    CHECK(!reg.dispatch("nonexistent", {}, &reply), "unknown service rejected");
    CHECK(reg.dispatch("time:u", {}, &reply), "time:u dispatches");
    CHECK(reply.size() == 8, "time reply is 8 bytes");

    uint64_t secs = 0;
    for (int i = 0; i < 8; ++i) secs |= static_cast<uint64_t>(reply[i]) << (8 * i);
    // 2020-01-01 .. 2100-01-01 in epoch seconds — sanity window
    CHECK(secs > 1577836800ull && secs < 4102444800ull, "time is a plausible epoch");
}

static void test_settings_service() {
    auto handler = make_settings_service();
    auto a = handler({});
    auto b = handler({});
    CHECK(a.size() == 4, "settings reply is 4 bytes");
    CHECK(a == b, "settings deterministic");
    CHECK(a[2] == 0x3c, "settings refresh 60Hz field");
}

static void test_names_sorted() {
    ServiceRegistry reg;
    reg.register_service("set:sys", make_settings_service());
    reg.register_service("time:u", make_time_service());
    const auto names = reg.names();
    CHECK(names.size() == 2, "two services");
    CHECK(names[0] == "set:sys" && names[1] == "time:u", "names sorted");
}

int main() {
    test_registry_basics();
    test_settings_service();
    test_names_sorted();
    if (failures == 0) {
        std::printf("test_horizon_host: ALL PASS\n");
        return 0;
    }
    std::printf("test_horizon_host: %d FAILURES\n", failures);
    return 1;
}
