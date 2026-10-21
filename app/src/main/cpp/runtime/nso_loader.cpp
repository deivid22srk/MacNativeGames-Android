#include "runtime/nso_loader.h"

#include <cstring>

namespace mng::nso {

static uint32_t rd_u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v; // little-endian host required (aarch64/x86-64 both fine)
}

bool parse_header(const uint8_t* data, size_t size, NsoHeader* out, std::string* error) {
    if (size < 0x100) {
        if (error) *error = "module smaller than NSO header (0x100)";
        return false;
    }
    if (rd_u32(data) != 0x304f534eu) { // 'NSO0'
        if (error) *error = "bad NSO magic";
        return false;
    }
    std::memset(out, 0, sizeof(*out));
    out->magic = rd_u32(data + 0x00);
    out->version = rd_u32(data + 0x04);
    out->reserved = rd_u32(data + 0x08);
    out->flags = rd_u32(data + 0x0c);

    const auto read_seg = [&](int idx, Segment* s) {
        const uint8_t* p = data + 0x10 + static_cast<size_t>(idx) * 12;
        s->memory_offset = rd_u32(p + 0);
        s->file_offset = rd_u32(p + 4);
        s->decompressed_size = rd_u32(p + 8);
    };
    read_seg(0, &out->text);
    read_seg(1, &out->rodata);
    read_seg(2, &out->data);

    out->bss_size = rd_u32(data + 0x3c);
    out->build_id_size = rd_u32(data + 0x40);

    // segment compressed sizes live at 0x60 (text), 0x64 (rodata), 0x68 (data)
    out->text.compressed_size = rd_u32(data + 0x60);
    out->rodata.compressed_size = rd_u32(data + 0x64);
    out->data.compressed_size = rd_u32(data + 0x68);

    // total module size (mapped), at 0x78
    out->total_size = rd_u32(data + 0x78);

    if (out->build_id_size > 0x40) {
        if (error) *error = "build id size unreasonable";
        return false;
    }
    return true;
}

bool lz4_decompress_block(const uint8_t* src, size_t src_size,
                          uint8_t* dst, size_t dst_capacity, size_t* dst_written) {
    size_t ip = 0, op = 0;
    while (ip < src_size) {
        const uint8_t token = src[ip++];
        size_t lit_len = token >> 4;
        if (lit_len == 15) {
            uint8_t b;
            do {
                if (ip >= src_size) return false;
                b = src[ip++];
                lit_len += b;
            } while (b == 255);
        }
        if (ip + lit_len > src_size || op + lit_len > dst_capacity) return false;
        std::memcpy(dst + op, src + ip, lit_len);
        ip += lit_len;
        op += lit_len;

        if (ip >= src_size) break; // last literals, no match follows
        if (ip + 2 > src_size) return false;
        const size_t offset = static_cast<size_t>(src[ip]) | (static_cast<size_t>(src[ip + 1]) << 8);
        ip += 2;
        if (offset == 0 || offset > op) return false; // corrupt / out-of-window

        size_t match_len = (token & 0x0f) + 4;
        if ((token & 0x0f) == 15) {
            uint8_t b;
            do {
                if (ip >= src_size) return false;
                b = src[ip++];
                match_len += b;
            } while (b == 255);
        }
        if (op + match_len > dst_capacity) return false;

        // overlapping copies allowed (byte-by-byte semantics)
        const size_t from = op - offset;
        for (size_t i = 0; i < match_len; ++i) {
            dst[op + i] = dst[from + i];
        }
        op += match_len;
    }
    if (dst_written) *dst_written = op;
    return true;
}

LoadResult load_module(const uint8_t* data, size_t size) {
    LoadResult res;
    NsoHeader hdr{};
    std::string err;
    if (!parse_header(data, size, &hdr, &err)) {
        res.error = err;
        return res;
    }

    const size_t image_size =
        static_cast<size_t>(hdr.text.memory_offset) + hdr.text.decompressed_size;
    const size_t ro_end =
        static_cast<size_t>(hdr.rodata.memory_offset) + hdr.rodata.decompressed_size;
    const size_t da_end =
        static_cast<size_t>(hdr.data.memory_offset) + hdr.data.decompressed_size;
    const size_t total = (image_size > ro_end ? image_size : ro_end) > da_end
                             ? (image_size > ro_end ? image_size : ro_end)
                             : da_end;

    res.image.assign(total + hdr.bss_size, 0x00);
    res.bss_start = total;
    res.bss_size = hdr.bss_size;

    const bool compressed[3] = {
        (hdr.flags & 1u) != 0,
        (hdr.flags & 2u) != 0,
        (hdr.flags & 4u) != 0,
    };
    const Segment* segs[3] = {&hdr.text, &hdr.rodata, &hdr.data};

    for (int i = 0; i < 3; ++i) {
        Segment s = *segs[i]; // local copy: uncompressed modules omit comp size
        if (!compressed[i] && s.compressed_size == 0) {
            s.compressed_size = s.decompressed_size;
        }
        const uint8_t* src = data + s.file_offset;
        uint8_t* dst = res.image.data() + s.memory_offset;
        if (compressed[i]) {
            size_t written = 0;
            if (!lz4_decompress_block(src, s.compressed_size, dst, s.decompressed_size, &written) ||
                written != s.decompressed_size) {
                res.error = "LZ4 segment decompression failed/mismatch";
                res.image.clear();
                return res;
            }
        } else {
            if (s.compressed_size == 0) s.compressed_size = s.decompressed_size;
            if (static_cast<size_t>(s.file_offset) + s.compressed_size > size ||
                s.compressed_size != s.decompressed_size) {
                res.error = "uncompressed segment bounds/size mismatch";
                res.image.clear();
                return res;
            }
            std::memcpy(dst, src, s.decompressed_size);
        }
    }

    res.ok = true;
    return res;
}

size_t apply_relr(uint8_t* image_base, size_t image_size,
                  const uint64_t* relr, size_t relr_count) {
    size_t applied = 0;
    uint64_t where = 0; // running address *within the image* of current bitmap
    for (size_t i = 0; i < relr_count; ++i) {
        const uint64_t entry = relr[i];
        if ((entry & 1ull) == 0) {
            // address entry: relocate the single 64-bit word at entry
            where = entry;
            if (where + 8 <= image_size) {
                uint64_t v;
                std::memcpy(&v, image_base + where, 8);
                v += reinterpret_cast<uint64_t>(image_base);
                std::memcpy(image_base + where, &v, 8);
                ++applied;
            }
            where += 8;
        } else {
            // bitmap entry: 63 slots relative to `where`
            for (uint64_t bit = 0; bit < 63; ++bit) {
                if (entry & (1ull << (bit + 1))) {
                    const uint64_t addr = where + bit * 8;
                    if (addr + 8 <= image_size) {
                        uint64_t v;
                        std::memcpy(&v, image_base + addr, 8);
                        v += reinterpret_cast<uint64_t>(image_base);
                        std::memcpy(image_base + addr, &v, 8);
                        ++applied;
                    }
                }
            }
        }
    }
    return applied;
}

} // namespace mng::nso
