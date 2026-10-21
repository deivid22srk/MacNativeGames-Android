package com.macnativegames.android

import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * M0 — container header sniffing for XCI (HFS0 secure partition) and NSP (PFS0),
 * plus NSO module headers. Pure JVM logic so it is unit-testable with synthetic
 * fixtures (no game data, no real keys — mirrors the upstream repo's test policy).
 *
 * Reference formats (public documentation):
 *  - PFS0: magic "PFS0", u32 fileCount, u32 stringTableSize, u32 padding, then entries.
 *  - HFS0: magic "HFS0", u32 fileCount, u32 stringTableSize, u32 padding, then entries.
 *  - NSO : magic "NSO0", then three module segment descriptors.
 */
object ContainerFormat {

    enum class Kind(val magic: String, val label: String) {
        NSP("PFS0", "NSP (PFS0 partition)"),
        XCI("HFS0", "XCI (HFS0 partition)"),
        NSO("NSO0", "NSO module"),
        UNKNOWN("", "Unknown container"),
    }

    data class HeaderInfo(
        val kind: Kind,
        val fileCount: Int,
        val stringTableSize: Int,
        val valid: Boolean,
        val notes: List<String>,
    )

    private const val HEADER_BYTES = 16

    /** Reads the first bytes of [input] and classifies the container. Closes nothing. */
    fun sniff(input: InputStream): HeaderInfo {
        val buf = ByteArray(HEADER_BYTES)
        var off = 0
        while (off < HEADER_BYTES) {
            val n = input.read(buf, off, HEADER_BYTES - off)
            if (n < 0) break
            off += n
        }
        return sniff(buf.copyOf(off))
    }

    /** Pure function over the first bytes of a file — the testable core. */
    fun sniff(firstBytes: ByteArray): HeaderInfo {
        if (firstBytes.size < 4) {
            return HeaderInfo(Kind.UNKNOWN, 0, 0, false, listOf("File too small to contain a container header"))
        }
        val magic = String(firstBytes, 0, 4, Charsets.US_ASCII)
        return when (magic) {
            Kind.NSP.magic, Kind.XCI.magic -> {
                val bb = ByteBuffer.wrap(firstBytes).order(ByteOrder.LITTLE_ENDIAN)
                bb.position(4)
                val fileCount = bb.int
                val stringTableSize = bb.int
                val notes = mutableListOf<String>()
                val valid = fileCount in 1..100_000 && stringTableSize in 0..0x1000_0000
                if (!valid) notes += "Header sanity check failed (fileCount=$fileCount, stringTableSize=$stringTableSize)"
                else notes += "${magic} header ok: $fileCount entries"
                HeaderInfo(kindFor(magic), fileCount, stringTableSize, valid, notes)
            }
            Kind.NSO.magic -> {
                val notes = mutableListOf("NSO0 module header detected (segments follow at +0x80)")
                HeaderInfo(Kind.NSO, 3, 0, true, notes)
            }
            else -> HeaderInfo(
                Kind.UNKNOWN, 0, 0, false,
                listOf("Magic '$magic' is not PFS0/HFS0/NSO0 — not a supported container")
            )
        }
    }

    private fun kindFor(magic: String): Kind =
        if (magic == Kind.NSP.magic) Kind.NSP else Kind.XCI
}
