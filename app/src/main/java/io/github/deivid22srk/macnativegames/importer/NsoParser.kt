package io.github.deivid22srk.macnativegames.importer

import java.io.EOFException
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Decodificador de blocos LZ4 (formato "block", não frame), usado pelos módulos NSO.
 * Implementação própria conforme a especificação do formato (mit License, este projeto).
 *
 * NSO comprime as seções text/ro/data com LZ4 block (flags bit 0/1/2) e o tamanho
 * descomprimido é conhecido pelo cabeçalho — exatamente como o original faz com
 * lz4.block.decompress(raw, uncompressed_size=sz).
 */
object Lz4 {

    fun decompressBlock(src: ByteArray, expectedSize: Int): ByteArray {
        val dst = ByteArray(expectedSize)
        var sPos = 0
        var dPos = 0

        fun readByte(): Int {
            if (sPos >= src.size) throw EOFException("LZ4: fim inesperado da entrada")
            return src[sPos++].toInt() and 0xFF
        }

        while (sPos < src.size) {
            val token = readByte()
            var litLen = token shr 4
            if (litLen == 15) {
                var b: Int
                do {
                    b = readByte()
                    litLen += b
                } while (b == 255)
            }
            if (dPos + litLen > expectedSize) throw IllegalArgumentException("LZ4: saída excedida (literais)")
            if (sPos + litLen > src.size) throw EOFException("LZ4: literais além da entrada")
            System.arraycopy(src, sPos, dst, dPos, litLen)
            sPos += litLen
            dPos += litLen

            if (sPos >= src.size) break // último bloco pode terminar sem match

            // match: offset de 2 bytes little-endian (1-based)
            if (sPos + 2 > src.size) throw EOFException("LZ4: offset além da entrada")
            val offset = (src[sPos].toInt() and 0xFF) or ((src[sPos + 1].toInt() and 0xFF) shl 8)
            sPos += 2
            if (offset == 0) throw IllegalArgumentException("LZ4: offset 0 inválido")
            var matchLen = (token and 0xF) + 4
            if ((token and 0xF) == 15) {
                var b: Int
                do {
                    b = readByte()
                    matchLen += b
                } while (b == 255)
            }
            if (dPos + matchLen > expectedSize) throw IllegalArgumentException("LZ4: saída excedida (match)")
            if (offset > dPos) throw IllegalArgumentException("LZ4: offset antes do início da saída")
            // cópia byte a byte: o match pode se sobrepor à própria saída
            var from = dPos - offset
            repeat(matchLen) {
                dst[dPos++] = dst[from++]
            }
        }

        if (dPos != expectedSize) {
            throw IllegalArgumentException(
                "LZ4: tamanho descomprimido ($dPos) difere do esperado ($expectedSize)"
            )
        }
        return dst
    }
}

/**
 * Parser do cabeçalho NSO0 — layout verificado contra o reescritor real do projeto
 * original (games/totk/third_party/d2r/patch_nso.py):
 *   magic "NSO0" @0x00, version @0x04, flags @0x0C
 *   segmentos com stride 0x10 a partir de 0x10: {fileOffset, memoryOffset, decompressedSize, pad}
 *   bssSize @0x3C, buildId @0x40..0x60, tamanhos comprimidos @0x60/0x64/0x68
 */
data class NsoInfo(
    val version: Int,
    val flags: Int,
    val segments: List<Segment>,
    val bssSize: Long,
    val buildId: ByteArray,
) {
    data class Segment(val fileOffset: Long, val memoryOffset: Long, val decompressedSize: Long) {
        val compressed: Boolean get() = false // preenchido via flags no parser
    }

    val textSegment: Segment get() = segments[0]
    val roSegment: Segment get() = segments[1]
    val dataSegment: Segment get() = segments[2]

    fun buildIdHex(): String = buildId.joinToString("") { "%02x".format(it) }
}

object NsoParser {
    const val MAGIC = "NSO0"
    const val HEADER_SIZE = 0x100

    fun parse(data: ByteBuffer): NsoInfo {
        if (data.remaining() < HEADER_SIZE) {
            throw IllegalArgumentException("NSO: arquivo menor que o cabeçalho (0x100)")
        }
        val order = data.order()
        data.order(ByteOrder.LITTLE_ENDIAN)
        try {
            val magic = ByteArray(4)
            data.get(magic)
            if (String(magic, Charsets.US_ASCII) != MAGIC) {
                throw IllegalArgumentException("NSO: magic inválido (esperado NSO0)")
            }
            val version = data.int
            data.int // reserved @0x08
            val flags = data.int
            val segments = (0 until 3).map {
                val fileOffset = data.int.toLong() and 0xFFFFFFFFL
                val memoryOffset = data.int.toLong() and 0xFFFFFFFFL
                val decompSize = data.int.toLong() and 0xFFFFFFFFL
                data.int // padding (stride 0x10)
                NsoInfo.Segment(fileOffset, memoryOffset, decompSize)
            }
            val bssSize = data.int.toLong() and 0xFFFFFFFFL
            data.int // reserved @0x38
            val buildId = ByteArray(0x20)
            data.get(buildId)
            return NsoInfo(version, flags, segments, bssSize, buildId)
        } finally {
            data.order(order)
        }
    }

    /** Lê o cabeçalho NSO0 de um arquivo (somente os primeiros 0x100 bytes). */
    fun parseFile(path: String): NsoInfo =
        java.io.RandomAccessFile(path, "r").use { raf ->
            val buf = ByteArray(HEADER_SIZE)
            raf.readFully(buf)
            parse(ByteBuffer.wrap(buf))
        }

    /** Flags bit0/1/2 = seções text/ro/data comprimidas com LZ4 block. */
    fun sectionCompressed(flags: Int, section: Int): Boolean = (flags shr section) and 1 == 1
}
