// MacNativeGames-Android — estruturas e utilidades de módulo NSO (M2 groundwork). MIT License.
//
// Estruturas espelham o formato real (verificado no reescritor original patch_nso.py):
// cabeçalho 0x100, segmentos com stride 0x10 em {file_offset, memory_offset, decomp_size},
// bss @0x3C, build-id @0x40, tamanhos comprimidos @0x60/0x64/0x68.
// A reescrita de instruções (x18/tpidr/svc/cntpct) é o M2: aqui está o pipeline de
// descompressão LZ4 block e os tipos, com testes sintéticos no host.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mng {

struct NsoSegment {
    uint32_t file_offset = 0;      // onde a seção (comprimida) está no arquivo
    uint32_t memory_offset = 0;    // destino dentro da imagem
    uint32_t decompressed_size = 0;
};

struct NsoHeader {
    static constexpr uint32_t kMagic = 0x304F534E; // "NSO0" little-endian
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t flags = 0;            // bit0/1/2: text/ro/data LZ4; bit3/4/5: hash check
    std::array<NsoSegment, 3> segments; // 0=text 1=ro 2=data
    uint32_t bss_size = 0;
    std::array<uint8_t, 0x20> build_id{};
    std::array<uint32_t, 3> compressed_sizes{};
};

/// Descompressão LZ4 block (formato raw, como o NSO usa).
/// Retorna false se a saída não preencher exatamente `expected_size` bytes.
bool Lz4DecompressBlock(const uint8_t* src, size_t src_size, uint8_t* dst, size_t expected_size);

/// Lê e valida o cabeçalho NSO dos primeiros 0x100 bytes.
bool ParseNsoHeader(const uint8_t* data, size_t size, NsoHeader* out);

/// Expande as seções do módulo (LZ4 onde marcado) para uma imagem contínua
/// no layout de memória (text+ro+data, cada uma em seu memory_offset).
/// ESTRITA: rejeita dados inconsistentes em vez de corromper memória.
bool ExpandNsoImage(const uint8_t* file_data, size_t file_size, NsoHeader* header,
                    std::vector<uint8_t>* image);

} // namespace mng
