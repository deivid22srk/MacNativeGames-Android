package io.github.deivid22srk.macnativegames.importer

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Entrada de um container PFS0 (NSP). */
data class Pfs0Entry(
    val dataOffset: Long,
    val size: Long,
    val name: String,
)

data class Pfs0Info(
    val fileCount: Int,
    val stringTableSize: Int,
    val entries: List<Pfs0Entry>,
) {
    /** Extensões únicas (minúsculas), sem conteúdo — usado para resumo sem expor nomes. */
    fun extensionSummary(): Map<String, Int> =
        entries.groupingBy { e -> e.name.substringAfterLast('.', "").lowercase() }
            .eachCount()
            .filterKeys { it.isNotBlank() }
}

/**
 * Parser de container PFS0 ("NSP"), conforme o formato documentado (switchbrew):
 *   magic "PFS0" @0x00, fileCount @0x04, stringTableSize @0x08, reserved @0x0C
 *   entradas de 0x18 bytes: {u64 offset, u64 size, u32 nameOffset, u32 pad}
 *   string table logo após as entradas.
 */
object Pfs0Parser {
    const val MAGIC = "PFS0"
    private const val HEADER = 0x10
    private const val ENTRY_SIZE = 0x18

    fun parse(data: ByteBuffer): Pfs0Info {
        if (data.remaining() < HEADER) {
            throw IllegalArgumentException("PFS0: arquivo menor que o cabeçalho")
        }
        val order = data.order()
        data.order(ByteOrder.LITTLE_ENDIAN)
        try {
            val magic = ByteArray(4)
            data.get(magic)
            if (String(magic, Charsets.US_ASCII) != MAGIC) {
                throw IllegalArgumentException("PFS0: magic inválido (esperado PFS0)")
            }
            val fileCount = data.int
            val stringTableSize = data.int
            data.int // reserved
            if (fileCount < 0 || fileCount > 100_000) {
                throw IllegalArgumentException("PFS0: contagem de arquivos suspeita ($fileCount)")
            }
            if (data.remaining() < fileCount * ENTRY_SIZE) {
                throw IllegalArgumentException("PFS0: tabela de entradas truncada")
            }
            val tableStart = HEADER
            val stringTableStart = tableStart + fileCount * ENTRY_SIZE
            if (data.remaining() < stringTableSize) {
                throw IllegalArgumentException("PFS0: string table truncada")
            }
            val entries = ArrayList<Pfs0Entry>(fileCount)
            for (i in 0 until fileCount) {
                val dataOffset = data.long
                val size = data.long
                val nameOffset = data.int
                data.int // pad
                val name = readCString(data, stringTableStart + nameOffset)
                entries.add(Pfs0Entry(dataOffset, size, name))
            }
            return Pfs0Info(fileCount, stringTableSize, entries)
        } finally {
            data.order(order)
        }
    }

    private fun readCString(data: ByteBuffer, absolutePos: Int): String {
        if (absolutePos >= data.capacity()) return ""
        val save = data.position()
        data.position(absolutePos)
        val sb = StringBuilder()
        while (data.hasRemaining()) {
            val b = data.get().toInt() and 0xFF
            if (b == 0) break
            sb.append(b.toChar())
        }
        data.position(save)
        return sb.toString()
    }
}
