package io.github.deivid22srk.macnativegames

import io.github.deivid22srk.macnativegames.importer.NsoParser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Fixtures sintéticas de NSO0 (geradas em código — nenhum dado de jogo).
 * Layout verificado contra o reescritor real do projeto original:
 * segmentos com stride 0x10 em {fileOffset, memoryOffset, decompSize, pad},
 * bss @0x3C, build-id @0x40, tamanhos comprimidos @0x60.
 */
class NsoParserTest {

    private fun buildHeader(
        version: Int = 1,
        flags: Int = 0b111, // text/ro/data comprimidos
        segs: List<Triple<Long, Long, Long>> = listOf(
            Triple(0x100L, 0x0L, 0x1000L),
            Triple(0x1200L, 0x1000L, 0x400L),
            Triple(0x1800L, 0x1400L, 0x800L),
        ),
        bss: Long = 0x200L,
        buildId: ByteArray = ByteArray(0x20) { (it + 1).toByte() },
        compressedSizes: List<Long> = listOf(0x900L, 0x300L, 0x700L),
    ): ByteBuffer {
        val buf = ByteBuffer.allocate(0x100).order(ByteOrder.LITTLE_ENDIAN)
        buf.put("NSO0".toByteArray(Charsets.US_ASCII))
        buf.putInt(version)
        buf.putInt(0)               // reserved @0x08
        buf.putInt(flags)
        segs.forEach { (fo, mo, sz) ->
            buf.putInt(fo.toInt()); buf.putInt(mo.toInt()); buf.putInt(sz.toInt()); buf.putInt(0)
        }
        buf.putInt(bss.toInt())     // @0x3C
        buf.putInt(0)               // reserved @0x38
        buf.put(buildId)            // @0x40..0x60
        compressedSizes.forEach { buf.putInt(it.toInt()) } // @0x60/0x64/0x68
        buf.position(0)
        return buf
    }

    @Test
    fun `parseia cabeçalho sintético completo`() {
        val info = NsoParser.parse(buildHeader())
        assertEquals(1, info.version)
        assertEquals(0b111, info.flags)
        assertEquals(0x0L, info.textSegment.memoryOffset)
        assertEquals(0x1000L, info.roSegment.memoryOffset)
        assertEquals(0x1400L, info.dataSegment.memoryOffset)
        assertEquals(0x1000L, info.textSegment.decompressedSize)
        assertEquals(0x200L, info.bssSize)
        assertEquals("0102030405060708090a0b0c0d0e0f10", info.buildIdHex().take(32))
    }

    @Test
    fun `build id tem 32 bytes`() {
        val info = NsoParser.parse(buildHeader())
        assertEquals(0x20, info.buildId.size)
        assertTrue(info.buildIdHex().length == 64)
    }

    @Test
    fun `flags indicam seções comprimidas`() {
        val flags = 0b101 // text e data comprimidos, ro não
        assertTrue(NsoParser.sectionCompressed(flags, 0))
        assertTrue(!NsoParser.sectionCompressed(flags, 1))
        assertTrue(NsoParser.sectionCompressed(flags, 2))
    }

    @Test
    fun `magic inválido rejeitado`() {
        val buf = buildHeader()
        buf.put(0, 'X'.code.toByte())
        assertThrows(IllegalArgumentException::class.java) { NsoParser.parse(buf) }
    }

    @Test
    fun `arquivo menor que o cabeçalho rejeitado`() {
        val buf = ByteBuffer.allocate(0x50)
        assertThrows(IllegalArgumentException::class.java) { NsoParser.parse(buf) }
    }
}
