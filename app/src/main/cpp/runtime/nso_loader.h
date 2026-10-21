// NSO module parsing for the Android port — M2 scaffolding.
//
// An NSO is a Switch code module: three segments (text/rodata/data) with
// optional LZ4 compression and RELR relative relocations. The upstream project
// rewrites these modules (x18 / thread pointer / svc / counter registers) with
// games/totk/third_party/d2r before mapping them. On Android the same rewrite
// pipeline applies; this file implements the *loader-side* mechanics:
//   1. parse the NSO0 header and segment table,
//   2. LZ4-decompress segments (own minimal block decoder, no external deps),
//   3. apply RELR relative relocations into guest memory.
//
// These are pure, deterministic functions over bytes — unit-tested on the host
// with synthetic fixtures (see tests/native/test_nso_loader.cpp). No game data
// is needed or used anywhere here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mng::nso {

struct Segment {
    uint32_t memory_offset; // offset within the mapped module
    uint32_t file_offset;   // offset within the NSO payload
    uint32_t decompressed_size;
    uint32_t compressed_size;
};

struct NsoHeader {
    uint32_t magic;          // 'NSO0'
    uint32_t version;
    uint32_t reserved;
    uint32_t flags;          // bit0: text compressed, bit1: rodata compressed, bit2: data compressed
    Segment text;
    Segment rodata;
    Segment data;
    uint32_t bss_size;
    uint32_t build_id_size;  // usually 0x20
    uint32_t total_size;
};

struct LoadResult {
    bool ok = false;
    std::string error;
    std::vector<uint8_t> image;       // assembled module image (text+rodata+data+bss zeroed)
    size_t bss_start = 0;
    size_t bss_size = 0;
    size_t relr_applied = 0;          // number of RELR entries consumed
};

// Parses and validates the 0x100-byte NSO header. Returns error text on failure.
bool parse_header(const uint8_t* data, size_t size, NsoHeader* out, std::string* error);

// Minimal LZ4 block decoder (RFC-free, matching LZ4 frameless block format used
// by NSO: token byte, literals, little-endian 16-bit match offset, etc.).
// Returns false on malformed input. This is a safety-critical primitive: every
// length is bounds-checked against the output buffer.
bool lz4_decompress_block(const uint8_t* src, size_t src_size,
                          uint8_t* dst, size_t dst_capacity, size_t* dst_written);

// Assembles the module image from raw (possibly compressed) payload bytes.
LoadResult load_module(const uint8_t* data, size_t size);

// Applies RELR relocation entries (relative addressing bitmap, per Switch ABI)
// against a module image mapped at `image_base` in guest memory.
// `relr` holds the raw RELR array read from the module's DYNAMIC segment —
// here passed explicitly because assembling full ELF dynamics is M2-later.
size_t apply_relr(uint8_t* image_base, size_t image_size,
                  const uint64_t* relr, size_t relr_count);

} // namespace mng::nso
