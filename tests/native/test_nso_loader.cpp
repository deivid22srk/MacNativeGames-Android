// M2 tests — NSO loader on synthetic fixtures (no game data).
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>

#include "runtime/nso_loader.h"

using mng::nso::apply_relr;
using mng::nso::load_module;
using mng::nso::lz4_decompress_block;
using mng::nso::NsoHeader;

static void wr_u32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    std::memcpy(v.data() + at, &x, 4);
}

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

// Encodes bytes as a valid LZ4 block of pure literals (a legal LZ4 encoding).
static std::vector<uint8_t> lz4_all_literals(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    size_t lit = raw.size();
    size_t chunks = 0;
    if (lit < 15) {
        out.push_back(static_cast<uint8_t>(lit << 4));
    } else {
        out.push_back(0xF0);
        size_t rem = lit - 15;
        while (rem >= 255) { out.push_back(255); rem -= 255; ++chunks; }
        out.push_back(static_cast<uint8_t>(rem));
    }
    out.insert(out.end(), raw.begin(), raw.end());
    return out;
}

static void test_lz4_roundtrip() {
    std::vector<uint8_t> raw(300);
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<uint8_t>(i * 7);
    std::vector<uint8_t> block = lz4_all_literals(raw);
    std::vector<uint8_t> out(raw.size() + 64, 0);
    size_t written = 0;
    bool ok = lz4_decompress_block(block.data(), block.size(), out.data(), out.size(), &written);
    CHECK(ok, "lz4 all-literals decodes");
    CHECK(written == raw.size(), "lz4 output size matches");
    CHECK(std::memcmp(out.data(), raw.data(), raw.size()) == 0, "lz4 roundtrip bytes equal");
}

static void test_lz4_match_case() {
    // literal "abc" + match(offset=3, len=6) => "abcabcabc"; token 0x32 = 3 literals + match nibble 2 (len 4+2)
    const uint8_t block[] = {0x32, 'a', 'b', 'c', 0x03, 0x00};
    uint8_t out[16] = {};
    size_t written = 0;
    bool ok = lz4_decompress_block(block, sizeof(block), out, sizeof(out), &written);
    CHECK(ok, "lz4 match decodes");
    CHECK(written == 9, "lz4 match length");
    CHECK(std::memcmp(out, "abcabcabc", 9) == 0, "lz4 match content");
}

static void test_lz4_rejects_corrupt() {
    // offset pointing beyond produced output (no history) must be rejected
    const uint8_t block[] = {0x30, 'a', 'b', 'c', 0x05, 0x00, 0x00 /*token: 0 match len*/};
    uint8_t out[16] = {};
    size_t written = 0;
    // offset 5 > op(3) -> corrupt; block: token 0x30=3literals, then match
    // match token comes from the same byte 0x30 -> match_len 4, offset 5 > 3 => reject
    bool ok = lz4_decompress_block(block, 5, out, sizeof(out), &written);
    CHECK(!ok, "lz4 rejects out-of-window offset");
}

static std::vector<uint8_t> make_nso(const std::vector<uint8_t>& text,
                                     const std::vector<uint8_t>& data,
                                     bool compress_text) {
    std::vector<uint8_t> nso(0x100, 0);
    wr_u32(nso, 0x00, 0x304f534e); // NSO0
    wr_u32(nso, 0x04, 0);          // version
    const uint32_t flags = compress_text ? 1u : 0u;
    wr_u32(nso, 0x0c, flags);

    // layout: text at 0x0, data at text end (rounded 0x10)
    const uint32_t data_off = static_cast<uint32_t>((text.size() + 0xF) & ~0xFu);
    // text segment descriptor at 0x10 (mem, file, size)
    wr_u32(nso, 0x10, 0);
    wr_u32(nso, 0x14, 0x100);
    wr_u32(nso, 0x18, static_cast<uint32_t>(text.size()));
    // rodata segment descriptor at 0x1c — empty in this fixture
    wr_u32(nso, 0x1c, static_cast<uint32_t>(text.size()));
    wr_u32(nso, 0x20, 0x100);
    wr_u32(nso, 0x24, 0);
    // data segment descriptor at 0x28 (file payload starts after the padded text)
    wr_u32(nso, 0x28, data_off);
    wr_u32(nso, 0x2c, 0x100 + data_off);
    wr_u32(nso, 0x30, static_cast<uint32_t>(data.size()));
    wr_u32(nso, 0x3c, 0);                      // bss
    wr_u32(nso, 0x40, 0x20);                   // build id size
    wr_u32(nso, 0x60, static_cast<uint32_t>(compress_text ? 0 : text.size()));  // text comp size
    wr_u32(nso, 0x64, 0);
    wr_u32(nso, 0x68, static_cast<uint32_t>(data.size()));                       // data comp size
    wr_u32(nso, 0x78, static_cast<uint32_t>(data_off + data.size()));            // total

    nso.insert(nso.end(), text.begin(), text.end());
    nso.resize(0x100 + text.size());
    nso.resize(0x100 + data_off, 0);
    nso.insert(nso.end(), data.begin(), data.end());
    return nso;
}

static void test_nso_parse_and_load() {
    std::vector<uint8_t> text = {0xd5, 0x0f, 0x00, 0x1e}; // dummy ARM64 insns
    std::vector<uint8_t> data = {'M', 'N', 'G'};
    auto nso = make_nso(text, data, /*compress_text=*/false);

    NsoHeader hdr{};
    std::string err;
    CHECK(mng::nso::parse_header(nso.data(), nso.size(), &hdr, &err), "header parses");
    CHECK(hdr.magic == 0x304f534e, "magic preserved");

    auto res = load_module(nso.data(), nso.size());
    CHECK(res.ok, "module loads");
    CHECK(res.image.size() >= data.size() + text.size(), "image covers segments");
    CHECK(std::memcmp(res.image.data(), text.data(), text.size()) == 0, "text segment bytes");
    // data lands at its memory offset (text end rounded)
    const size_t data_off = (text.size() + 0xF) & ~static_cast<size_t>(0xF);
    CHECK(std::memcmp(res.image.data() + data_off, data.data(), data.size()) == 0,
          "data segment bytes");
}

static void test_nso_rejects_bad_magic() {
    std::vector<uint8_t> nso(0x100, 0);
    wr_u32(nso, 0x00, 0x12345678);
    NsoHeader hdr{};
    std::string err;
    CHECK(!mng::nso::parse_header(nso.data(), nso.size(), &hdr, &err), "bad magic rejected");
}

static void test_relr_apply() {
    std::vector<uint8_t> image(64, 0);
    const uint64_t stored = 0x1000;      // relative value at offset 8
    std::memcpy(image.data() + 8, &stored, 8);
    std::memcpy(image.data() + 24, &stored, 8); // bitmap-covered slot

    const uint64_t relr_fixed[2] = {8, 5}; // entry0: address 8; entry1: bitmap (bit0 set) with slot bit1 => 24
    const size_t applied = apply_relr(image.data(), image.size(), relr_fixed, 2);
    CHECK(applied == 2, "relr applied two entries");

    uint64_t v1, v2;
    std::memcpy(&v1, image.data() + 8, 8);
    std::memcpy(&v2, image.data() + 24, 8);
    const uint64_t base = reinterpret_cast<uint64_t>(image.data());
    CHECK(v1 == base + 0x1000, "address-entry relocated");
    CHECK(v2 == base + 0x1000, "bitmap-entry relocated");
}

int main() {
    test_lz4_roundtrip();
    test_lz4_match_case();
    test_lz4_rejects_corrupt();
    test_nso_parse_and_load();
    test_nso_rejects_bad_magic();
    test_relr_apply();
    if (failures == 0) {
        std::printf("test_nso_loader: ALL PASS\n");
        return 0;
    }
    std::printf("test_nso_loader: %d FAILURES\n", failures);
    return 1;
}
