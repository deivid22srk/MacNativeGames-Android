// MacNativeGames-Android — implementação NSO (groundwork do M2). MIT License.
#include "nso_module.h"

#include <cstring>

namespace mng {

bool Lz4DecompressBlock(const uint8_t* src, size_t src_size, uint8_t* dst, size_t expected_size) {
    size_t s = 0, d = 0;
    auto read_byte = [&](uint8_t* out) -> bool {
        if (s >= src_size) return false;
        *out = src[s++];
        return true;
    };
    while (s < src_size) {
        uint8_t token = 0;
        if (!read_byte(&token)) return false;
        size_t lit_len = token >> 4;
        if (lit_len == 15) {
            uint8_t ext = 255;
            while (ext == 255) {
                if (!read_byte(&ext)) return false;
                lit_len += ext;
            }
        }
        if (d + lit_len > expected_size || s + lit_len > src_size) return false;
        std::memcpy(dst + d, src + s, lit_len);
        s += lit_len;
        d += lit_len;
        if (s == src_size) break;
        if (s + 2 > src_size) return false;
        uint32_t offset = static_cast<uint32_t>(src[s]) | (static_cast<uint32_t>(src[s + 1]) << 8);
        s += 2;
        if (offset == 0 || offset > d) return false;
        size_t match_len = (token & 0xF) + 4;
        if ((token & 0xF) == 15) {
            uint8_t ext = 255;
            while (ext == 255) {
                if (!read_byte(&ext)) return false;
                match_len += ext;
            }
        }
        if (d + match_len > expected_size) return false;
        size_t from = d - offset;
        for (size_t i = 0; i < match_len; ++i) dst[d + i] = dst[from + i];
        d += match_len;
    }
    return d == expected_size;
}

bool ParseNsoHeader(const uint8_t* data, size_t size, NsoHeader* out) {
    if (size < 0x100 || out == nullptr) return false;
    uint32_t magic = 0;
    std::memcpy(&magic, data, 4);
    if (magic != NsoHeader::kMagic) return false;
    std::memcpy(&out->version, data + 0x04, 4);
    std::memcpy(&out->flags, data + 0x0C, 4);
    for (int i = 0; i < 3; ++i) {
        const uint8_t* p = data + 0x10 + i * 0x10;
        std::memcpy(&out->segments[i].file_offset, p, 4);
        std::memcpy(&out->segments[i].memory_offset, p + 4, 4);
        std::memcpy(&out->segments[i].decompressed_size, p + 8, 4);
    }
    std::memcpy(&out->bss_size, data + 0x3C, 4);
    std::memcpy(out->build_id.data(), data + 0x40, 0x20);
    std::memcpy(out->compressed_sizes.data(), data + 0x60, 12);
    return true;
}

bool ExpandNsoImage(const uint8_t* file_data, size_t file_size, NsoHeader* header,
                    std::vector<uint8_t>* image) {
    if (file_data == nullptr || header == nullptr || image == nullptr) return false;
    // tamanho final: fim do segmento data (+ bss), arredondado para baixo a 4 KiB guest
    uint32_t end = 0;
    for (const auto& seg : header->segments) {
        uint32_t segEnd = seg.memory_offset + seg.decompressed_size;
        if (segEnd > end) end = segEnd;
    }
    image->assign(end, 0);
    for (int i = 0; i < 3; ++i) {
        const auto& seg = header->segments[i];
        if (seg.file_offset > file_size) return false;
        uint64_t avail = file_size - seg.file_offset;
        if (header->compressed_sizes[i] > avail) return false;
        const uint8_t* raw = file_data + seg.file_offset;
        uint8_t* dst = image->data() + seg.memory_offset;
        bool compressed = (header->flags >> i) & 1;
        if (compressed) {
            if (!Lz4DecompressBlock(raw, header->compressed_sizes[i], dst,
                                    seg.decompressed_size)) {
                return false;
            }
        } else {
            if (header->compressed_sizes[i] != seg.decompressed_size) return false;
            std::memcpy(dst, raw, seg.decompressed_size);
        }
    }
    // BSS permanece zerado.
    return true;
}

} // namespace mng
