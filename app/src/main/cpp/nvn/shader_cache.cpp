#include "nvn/shader_cache.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace mng::nvn {

namespace {

// Minimal SHA-256 (public-domain style implementation, self-contained so the
// host tests and the device build agree bit-for-bit).
struct Sha256 {
    uint32_t h[8];
    uint64_t len = 0;
    uint8_t buf[64];
    uint32_t buf_len = 0;

    Sha256() {
        static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                         0xa54ff53a, 0x510e527f, 0x9b05688c,
                                         0x1f83d9ab, 0x5be0cd19};
        memcpy(h, init, sizeof(h));
    }

    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const uint8_t* p) {
        static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
            0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
            0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
            0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
                   (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const uint8_t* data, size_t n) {
        len += n;
        while (n > 0) {
            uint32_t take = uint32_t((64 - buf_len) < n ? (64 - buf_len) : n);
            memcpy(buf + buf_len, data, take);
            buf_len += take;
            data += take;
            n -= take;
            if (buf_len == 64) {
                block(buf);
                buf_len = 0;
            }
        }
    }

    void final(uint8_t out[32]) {
        uint64_t bits = len * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (buf_len != 56) update(&zero, 1);
        uint8_t lenb[8];
        for (int i = 0; i < 8; ++i) lenb[i] = uint8_t(bits >> (56 - i * 8));
        update(lenb, 8);
        for (int i = 0; i < 8; ++i) {
            out[i * 4] = uint8_t(h[i] >> 24);
            out[i * 4 + 1] = uint8_t(h[i] >> 16);
            out[i * 4 + 2] = uint8_t(h[i] >> 8);
            out[i * 4 + 3] = uint8_t(h[i]);
        }
    }
};

std::string hex(const uint8_t* d, size_t n) {
    static const char* kHex = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s.push_back(kHex[d[i] >> 4]);
        s.push_back(kHex[d[i] & 0xf]);
    }
    return s;
}

} // namespace

ShaderCache::ShaderCache(std::string directory) : dir_(std::move(directory)) {
    mkdir(dir_.c_str(), 0755); // best effort; store() reports real failures
}

std::string ShaderCache::key_for(const uint32_t* words, size_t word_count) {
    Sha256 s;
    s.update(reinterpret_cast<const uint8_t*>(words), word_count * sizeof(uint32_t));
    uint8_t d[32];
    s.final(d);
    return hex(d, 32);
}

bool ShaderCache::load(const std::string& key, std::vector<uint32_t>& out) const {
    const std::string path = dir_ + "/" + key + ".spv";
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size % 4 != 0) {
        fclose(f);
        return false;
    }
    out.resize(size / 4);
    const size_t got = fread(out.data(), 4, out.size(), f);
    fclose(f);
    return got == out.size();
}

bool ShaderCache::store(const std::string& key, const uint32_t* words,
                        size_t word_count) {
    const std::string path = dir_ + "/" + key + ".spv";
    struct stat st{};
    if (stat(path.c_str(), &st) == 0) return true; // already cached
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (f == nullptr) return false;
    const size_t wrote = fwrite(words, sizeof(uint32_t), word_count, f);
    if (fclose(f) != 0 || wrote != word_count) {
        unlink(tmp.c_str());
        return false;
    }
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

uint32_t ShaderCache::entry_count() const {
    DIR* d = opendir(dir_.c_str());
    if (d == nullptr) return 0;
    uint32_t n = 0;
    while (auto* e = readdir(d)) {
        const size_t len = strlen(e->d_name);
        if (len > 4 && strcmp(e->d_name + len - 4, ".spv") == 0) ++n;
    }
    closedir(d);
    return n;
}

} // namespace mng::nvn
