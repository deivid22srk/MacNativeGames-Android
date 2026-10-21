package io.github.deivid22srk.macnativegames

import io.github.deivid22srk.macnativegames.importer.Lz4
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

/**
 * Blocos LZ4 sintéticos, codificados à mão conforme a especificação do formato
 * (mesmo formato usado nas seções NSO). Nenhum dado de jogo.
 */
class Lz4Test {

    @Test
    fun `bloco apenas com literais`() {
        // token 0x03 = 3 literais, sem match
        val src = byteArrayOf(0x03, 'a'.code.toByte(), 'b'.code.toByte(), 'c'.code.toByte())
        val dst = Lz4.decompressBlock(src, 3)
        assertArrayEquals("abc".toByteArray(), dst)
    }

    @Test
    fun `match com sobreposição`() {
        // token 0x41: 4 literais + match de 5 bytes; offset 4 (LE 04 00)
        val src = byteArrayOf(
            0x41,
            'a'.code.toByte(), 'b'.code.toByte(), 'c'.code.toByte(), 'd'.code.toByte(),
            0x04, 0x00,
        )
        val dst = Lz4.decompressBlock(src, 9)
        assertArrayEquals("abcdabcda".toByteArray(), dst)
    }

    @Test
    fun `literal length estendida com byte 255`() {
        // token 0xF0 → 15 + 255 + 1 = 271 literais
        val n = 271
        val src = ByteArray(1 + 2 + n)
        src[0] = 0xF0.toByte()
        src[1] = 255.toByte()
        src[2] = 1
        for (i in 0 until n) src[3 + i] = ((i * 31) and 0xFF).toByte()
        val dst = Lz4.decompressBlock(src, n)
        assertEquals(n, dst.size)
        assertEquals(((0 * 31) and 0xFF).toByte(), dst[0])
        assertEquals(((270 * 31) and 0xFF).toByte(), dst[270])
    }

    @Test
    fun `match length estendida`() {
        // token 0x4F: 4 literais, match 15+? → estendido com byte 3 → 15+3+4 = 22? (4+15 base = 19, +3 = 22)
        val src = byteArrayOf(
            0x4F.toByte(),
            'w'.code.toByte(), 'x'.code.toByte(), 'y'.code.toByte(), 'z'.code.toByte(),
            0x04, 0x00,
            3,
        )
        // match len = 15 + 4 + 3 = 22 → "wxyz" + 18 bytes copiados com overlap
        val dst = Lz4.decompressBlock(src, 4 + 22)
        assertEquals(26, dst.size)
        assertEquals("wxyz", String(dst, 0, 4, Charsets.US_ASCII))
        // cópia com overlap de período 4
        assertEquals('w'.code.toByte(), dst[4])
        assertEquals('z'.code.toByte(), dst[7])
        assertEquals('y'.code.toByte(), dst[25])
    }

    @Test
    fun `tamanho incompatível é rejeitado`() {
        val src = byteArrayOf(0x03, 'a'.code.toByte(), 'b'.code.toByte(), 'c'.code.toByte())
        assertThrows(IllegalArgumentException::class.java) { Lz4.decompressBlock(src, 10) }
    }

    @Test
    fun `sequência com dois tokens`() {
        // primeiro: 2 literais; segundo: 2 literais + match de 4 (offset 4)
        val src = byteArrayOf(
            0x22, 'h'.code.toByte(), 'i'.code.toByte(),
            0x20, 'o'.code.toByte(), 'k'.code.toByte(), 0x04, 0x00,
        )
        val dst = Lz4.decompressBlock(src, 8)
        assertArrayEquals("hiokhiok".toByteArray(), dst)
    }
}
