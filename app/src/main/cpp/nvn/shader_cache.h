// M3/M5 — on-disk SPIR-V shader cache.
//
// The guest pipeline emits translated shaders (Ryujinx's translator produces
// SPIR-V directly — no SPIRV-Cross needed on Android). Compiling SPIR-V to a
// driver pipeline is expensive on mobile; this cache stores compiled words
// keyed by the SHA-256 of the source words so subsequent boots hit disk
// instead of the translator.
//
// Layout: <dir>/<key hex>.spv — atomic write (tmp + rename) so a crash never
// leaves a torn entry.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mng::nvn {

class ShaderCache {
public:
    explicit ShaderCache(std::string directory);

    // SHA-256 of the SPIR-V words (canonical key).
    static std::string key_for(const uint32_t* words, size_t word_count);

    // true on hit; out receives the cached words.
    bool load(const std::string& key, std::vector<uint32_t>& out) const;

    // true on successful store (first write wins; later writes are no-ops).
    bool store(const std::string& key, const uint32_t* words, size_t word_count);

    // Count of cache entries (diagnostics).
    uint32_t entry_count() const;

    const std::string& directory() const { return dir_; }

private:
    std::string dir_;
};

} // namespace mng::nvn
