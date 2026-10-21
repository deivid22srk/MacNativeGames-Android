package com.macnativegames.android

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

/**
 * M0 unit tests — synthetic fixtures only. No game data, no real keys,
 * mirroring the upstream repo's container-test policy.
 */
class M0ValidationTest {

    private fun pfs0(fileCount: Int = 2, stringTableSize: Int = 64): ByteArray {
        val bb = ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN)
        bb.put("PFS0".toByteArray(Charsets.US_ASCII))
        bb.putInt(fileCount)
        bb.putInt(stringTableSize)
        bb.putInt(0)
        return bb.array()
    }

    private fun hfs0(fileCount: Int = 4, stringTableSize: Int = 128): ByteArray {
        val bb = ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN)
        bb.put("HFS0".toByteArray(Charsets.US_ASCII))
        bb.putInt(fileCount)
        bb.putInt(stringTableSize)
        bb.putInt(0)
        return bb.array()
    }

    @Test
    fun `sniff detects NSP PFS0 header`() {
        val info = ContainerFormat.sniff(pfs0())
        assertEquals(ContainerFormat.Kind.NSP, info.kind)
        assertTrue(info.valid)
        assertEquals(2, info.fileCount)
    }

    @Test
    fun `sniff detects XCI HFS0 header`() {
        val info = ContainerFormat.sniff(hfs0())
        assertEquals(ContainerFormat.Kind.XCI, info.kind)
        assertTrue(info.valid)
        assertEquals(4, info.fileCount)
    }

    @Test
    fun `sniff rejects insane file count`() {
        val info = ContainerFormat.sniff(pfs0(fileCount = 10_000_000))
        assertEquals(ContainerFormat.Kind.NSP, info.kind)
        assertFalse(info.valid)
    }

    @Test
    fun `sniff rejects unknown magic`() {
        val info = ContainerFormat.sniff("XXXX".toByteArray() + ByteArray(12))
        assertEquals(ContainerFormat.Kind.UNKNOWN, info.kind)
        assertFalse(info.valid)
    }

    @Test
    fun `sniff rejects too-small input`() {
        val info = ContainerFormat.sniff(byteArrayOf(0x50, 0x46))
        assertEquals(ContainerFormat.Kind.UNKNOWN, info.kind)
        assertFalse(info.valid)
    }

    @Test
    fun `sniff accepts NSO0 header`() {
        val info = ContainerFormat.sniff("NSO0".toByteArray() + ByteArray(60))
        assertEquals(ContainerFormat.Kind.NSO, info.kind)
        assertTrue(info.valid)
    }

    @Test
    fun `streaming sha256 matches MessageDigest on multi-chunk input`() {
        val data = ByteArray(3 * 1024 * 1024 + 7) { (it * 31).toByte() }
        val viaStreaming = StreamingHash.sha256(ByteArrayInputStream(data))
        val direct = MessageDigest.getInstance("SHA-256").digest(data)
            .joinToString("") { "%02x".format(it) }
        assertEquals(direct, viaStreaming)
    }

    @Test
    fun `keys parser keeps names only and reports missing`() {
        val fake = """
            # synthetic fixture — never a real key
            master_key_00 = 000102030405060708090a0b0c0d0e0f
            titlekek_source = f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff
            header_key_source = 0102030405060708090a0b0c0d0e0f10
            header_kek_source = 1112131415161718191a1b1c1d1e1f20
            key_area_key_application_source = 2122232425262728292a2b2c2d2e2f30
            unrelated_key = deadbeef
        """.trimIndent()
        val parser = ProdKeysParser.parse(ByteArrayInputStream(fake.toByteArray()))
        assertTrue(parser.isValid)
        assertTrue(parser.keyNames.contains("unrelated_key"))
        // toString must not leak any value:
        assertFalse(parser.toString().contains("deadbeef"))
        assertFalse(parser.toString().contains("00010203"))
    }

    @Test
    fun `keys parser flags missing required names`() {
        val fake = "master_key_00 = 000102030405060708090a0b0c0d0e0f\n"
        val parser = ProdKeysParser.parse(ByteArrayInputStream(fake.toByteArray()))
        assertFalse(parser.isValid)
        assertTrue(parser.missingRequired.contains("titlekek_source"))
        assertFalse(parser.toString().contains("000102030405"))
    }

    @Test
    fun `validator produces sanitized report`() {
        val data = pfs0() + ByteArray(4096) { 0x42 }
        val keysFixture = ProdKeysParser.REQUIRED.joinToString("\n") { name ->
            "$name = 000102030405060708090a0b0c0d0e0f"
        }
        val report = GameFileValidator.validate(
            ByteArrayInputStream(data), "synthetic.nsp", data.size.toLong(),
            ByteArrayInputStream(keysFixture.toByteArray())
        )
        val lines = report.toSanitizedLines()
        assertTrue(lines.any { it.contains("PASS") })
        assertTrue(lines.any { it.startsWith("sha256:") })
    }

    @Test
    fun `validator reports PARTIAL when required keys are missing`() {
        val data = pfs0() + ByteArray(1024) { 0x42 }
        val report = GameFileValidator.validate(
            ByteArrayInputStream(data), "synthetic.nsp", data.size.toLong(),
            ByteArrayInputStream("master_key_00 = 000102030405060708090a0b0c0d0e0f".toByteArray())
        )
        assertTrue(report.toSanitizedLines().any { it.contains("PARTIAL") })
    }

    @Test
    fun `validator reports FAIL for invalid container`() {
        val data = "JUNK".toByteArray() + ByteArray(128)
        val report = GameFileValidator.validate(
            ByteArrayInputStream(data), "synthetic.bin", data.size.toLong(), null
        )
        assertTrue(report.toSanitizedLines().any { it.contains("FAIL") })
    }
}
