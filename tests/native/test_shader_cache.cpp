// M3/M5 tests — SPIR-V shader cache (keying, store/load, atomicity hints).
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include <vector>

#include "nvn/shader_cache.h"

using mng::nvn::ShaderCache;

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
    namespace fs = std::filesystem;
    const std::string dir =
        (fs::temp_directory_path() / ("mng_shader_cache_test_" + std::to_string(::getpid())))
            .string();

    // Known-answer test for the key: SHA-256 over 4 bytes of 0x00000000.
    const uint32_t one_word[1] = {0};
    const std::string kEmpty = ShaderCache::key_for(one_word, 1);
    CHECK(kEmpty.size() == 64, "key is 64 hex chars");
    CHECK(kEmpty ==
              "df3f619804a92fdb4057192dc43dd748ea778adc52bc498ce80524c014b81119",
          "sha256(4 zero bytes) known answer");

    ShaderCache cache(dir);
    std::vector<uint32_t> blob = {0x07230203, 0x00010000, 0xdeadbeef, 0x12345678};
    const std::string key = ShaderCache::key_for(blob.data(), blob.size());

    std::vector<uint32_t> out;
    CHECK(!cache.load(key, out), "miss before store");
    CHECK(cache.store(key, blob.data(), blob.size()), "store succeeds");
    CHECK(cache.load(key, out), "hit after store");
    CHECK(out == blob, "round-trip preserves words");
    CHECK(cache.entry_count() == 1, "entry counted");
    CHECK(cache.store(key, blob.data(), blob.size()), "re-store is idempotent");
    CHECK(cache.entry_count() == 1, "no duplicate entry");

    // Distinct content → distinct key.
    std::vector<uint32_t> blob2 = blob;
    blob2[2] ^= 1u;
    const std::string key2 = ShaderCache::key_for(blob2.data(), blob2.size());
    CHECK(key2 != key, "different words → different key");

    std::error_code ec;
    fs::remove_all(dir, ec);
    if (failures == 0) std::printf("shader_cache tests PASSED\n");
    return failures == 0 ? 0 : 1;
}
