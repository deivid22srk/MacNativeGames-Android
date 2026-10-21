package io.github.deivid22srk.macnativegames

import io.github.deivid22srk.macnativegames.importer.Pfs0Parser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Fixtures sintéticas de PFS0 (NSP) geradas em código — nenhum dado de jogo. */
class Pfs0ParserTest {

    private fun buildPfs0(files: List<Pair<String, Int>>): ByteBuffer {
        val stringTable = ByteArrayOutputStream()
        val nameOffsets = IntArray(files.size)
        files.forEachIndexed { i, (name, _) ->
            nameOffsets[i] = stringTable.size()
            stringTable.write(name.toByteArray(Charsets.US_ASCII))
            stringTable.write(0)
        }
        val st = stringTable.toByteArray()
        val out = ByteArrayOutputStream()
        val header = ByteBuffer.allocate(0x10).order(ByteOrder.LITTLE_ENDIAN)
        header.put("PFS0".toByteArray(Charsets.US_ASCII))
        header.putInt(files.size)
        header.putInt(st.size)
        header.putInt(0)
        out.write(header.array())
        files.forEachIndexed { i, (_, size) ->
            val e = ByteBuffer.allocate(0x18).order(ByteOrder.LITTLE_ENDIAN)
            e.putLong(0x1000L * (i + 1)) // offset sintético
            e.putLong(size.toLong())
            e.putInt(nameOffsets[i])
            e.putInt(0)
            out.write(e.array())
        }
        out.write(st)
        return ByteBuffer.wrap(out.toByteArray()).order(ByteOrder.LITTLE_ENDIAN)
    }

    @Test
    fun `parseia NSP sintético com 3 entradas`() {
        val info = Pfs0Parser.parse(
            buildPfs0(listOf("aaa.nca" to 4096, "bbb.nca" to 8192, "ccc.tik" to 512))
        )
        assertEquals(3, info.fileCount)
        assertEquals(0x1000L, info.entries[0].dataOffset)
        assertEquals(4096L, info.entries[0].size)
        assertEquals("aaa.nca", info.entries[0].name)
        assertEquals("ccc.tik", info.entries[2].name)
    }

    @Test
    fun `resumo de extensões sem expor nomes`() {
        val info = Pfs0Parser.parse(
            buildPfs0(listOf("a.nca" to 1, "b.nca" to 2, "c.tik" to 3))
        )
        val ext = info.extensionSummary()
        assertEquals(2, ext["nca"])
        assertEquals(1, ext["tik"])
    }

    @Test
    fun `magic inválido rejeitado`() {
        val buf = buildPfs0(listOf("x.nca" to 1))
        buf.put(0, 'Z'.code.toByte())
        assertThrows(IllegalArgumentException::class.java) { Pfs0Parser.parse(buf) }
    }

    @Test
    fun `contagem absurda rejeitada`() {
        val buf = buildPfs0(listOf("x.nca" to 1))
        buf.order(ByteOrder.LITTLE_ENDIAN).putInt(4, 1_000_000)
        assertThrows(IllegalArgumentException::class.java) { Pfs0Parser.parse(buf) }
    }
}
