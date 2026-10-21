package io.github.deivid22srk.macnativegames

import io.github.deivid22srk.macnativegames.importer.HashValidator
import io.github.deivid22srk.macnativegames.importer.KeysManager
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

/** Vetores conhecidos de SHA-256 (RFC 6234 / NIST) + validação de chaves sem expor valores. */
class ImportersTest {

    @get:Rule
    val tmp = TemporaryFolder()

    @Test
    fun `sha256 de arquivo vazio`() {
        val f = File(tmp.root, "empty.bin").apply { writeBytes(ByteArray(0)) }
        assertEquals(
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            HashValidator.sha256(f),
        )
    }

    @Test
    fun `sha256 de abc`() {
        val f = File(tmp.root, "abc.bin").apply { writeText("abc") }
        assertEquals(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            HashValidator.sha256(f),
        )
    }

    @Test
    fun `sha256 de arquivo maior que um chunk`() {
        // 12 MiB de bytes determinísticos: verifica streaming multi-chunk (8 MiB por leitura)
        val f = File(tmp.root, "big.bin")
        val part = ByteArray(4 * 1024 * 1024) { (it % 251).toByte() }
        f.outputStream().use { outs ->
            repeat(3) { outs.write(part) }
        }
        val hex = HashValidator.sha256(f)
        assertEquals(64, hex.length)
        // determinístico: segunda passada igual
        assertEquals(hex, HashValidator.sha256(f))
    }

    @Test
    fun `verificação com hash esperado`() {
        val f = File(tmp.root, "v.bin").apply { writeText("abc") }
        assertTrue(HashValidator.verify(f, "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"))
        assertFalse(HashValidator.verify(f, "0000000000000000000000000000000000000000000000000000000000000000"))
    }

    @Test
    fun `prod keys sintético é parseado e validado sem valores`() {
        val f = File(tmp.root, "prod.keys")
        f.writeText(
            """
            # comentário
            header_key = 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff
            master_key_00 = deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef
            titlekek_00 = 0102030405060708090a0b0c0d0e0f10
            key_area_key_application_00 = aabbccddeeff00112233445566778899
            key_area_key_application_01 = aabbccddeeff00112233445566778898
            key_area_key_ocean_00 = 11223344556677889900aabbccddeeff
            key_area_key_system_00 = 99887766554433221100ffeeddccbbaa
            linha_invalida_sem_igual
            """.trimIndent()
        )
        val map = KeysManager.parse(f)
        val info = KeysManager.validate(map)
        assertEquals(7, info.totalKeys)
        assertTrue(info.hasHeaderKey)
        assertTrue(info.hasMasterKey00)
        assertTrue(info.hasTitlekek00)
        assertEquals(2, info.keyAreaKeyApplicationCount)
        val desc = KeysManager.describe(info)
        assertFalse(desc.contains("deadbeef")) // nunca expõe valores
        assertFalse(desc.contains("00112233"))
    }
}
