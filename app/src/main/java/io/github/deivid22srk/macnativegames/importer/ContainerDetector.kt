package io.github.deivid22srk.macnativegames.importer

import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

/** Tipo detectado de container/imagem fornecida pelo usuário. */
enum class RomKind { NSP, XCI, NSO, UNKNOWN }

data class RomInfo(
    val kind: RomKind,
    val sizeBytes: Long,
    val pfs0: Pfs0Info?,
    val nso: NsoInfo?,
    val xciHeadMagic: Boolean,
    val xciHfs0At0x1000: Boolean,
)

/**
 * Detecção e validação estrutural (M0) dos containers aceitos pelo projeto original:
 * NSP (PFS0), XCI (gamecard "HEAD" @0x100 + HFS0 heurístico @0x1000) e NSO solto.
 *
 * Nesta fase **não** há descriptografia nem extração de conteúdo do jogo — apenas
 * estrutura. A extração completa (hactool portado para NDK) é o marco M2.
 */
object ContainerDetector {
    const val XCI_HEAD_OFFSET = 0x100L
    const val XCI_HFS0_PROBE_OFFSET = 0x1000L

    private fun readMagicAt(file: File, offset: Long, len: Int = 4): String? {
        if (file.length() < offset + len) return null
        return RandomAccessFile(file, "r").use { raf ->
            raf.seek(offset)
            val buf = ByteArray(len)
            val n = raf.read(buf)
            if (n < len) null else String(buf, Charsets.US_ASCII)
        }
    }

    fun detect(file: File): RomInfo {
        val head0 = readMagicAt(file, 0) ?: ""
        val kind = when {
            head0 == Pfs0Parser.MAGIC -> RomKind.NSP
            head0 == NsoParser.MAGIC -> RomKind.NSO
            readMagicAt(file, XCI_HEAD_OFFSET) == "HEAD" -> RomKind.XCI
            else -> RomKind.UNKNOWN
        }
        var pfs0: Pfs0Info? = null
        var nso: NsoInfo? = null
        var xciHead = false
        var xciHfs0 = false
        when (kind) {
            RomKind.NSP -> {
                // Cabeçalho + entradas + string table podem ser grandes; lê até 4 MiB.
                val cap = minOf(file.length(), 4L * 1024 * 1024).toInt()
                val buf = ByteBuffer.allocate(cap).order(ByteOrder.LITTLE_ENDIAN)
                RandomAccessFile(file, "r").use { raf ->
                    raf.readFully(buf.array())
                }
                pfs0 = Pfs0Parser.parse(buf)
            }
            RomKind.NSO -> nso = NsoParser.parseFile(file.absolutePath)
            RomKind.XCI -> {
                xciHead = true
                xciHfs0 = readMagicAt(file, XCI_HFS0_PROBE_OFFSET) == "HFS0"
            }
            RomKind.UNKNOWN -> {}
        }
        return RomInfo(kind, file.length(), pfs0, nso, xciHead, xciHfs0)
    }

    fun describe(info: RomInfo): String = when (info.kind) {
        RomKind.NSP -> {
            val p = info.pfs0!!
            val ext = p.extensionSummary().entries.joinToString(", ") { "${it.value}x .${it.key}" }
            "NSP (PFS0): ${p.fileCount} arquivos [${ext.ifEmpty { "sem extensão" }}]"
        }
        RomKind.NSO -> {
            val n = info.nso!!
            "NSO: 3 segmentos, bss=${n.bssSize}, build-id=${n.buildIdHex().take(16)}…"
        }
        RomKind.XCI ->
            "XCI: magic HEAD@0x100 ok, HFS0@0x1000=${if (info.xciHfs0At0x1000) "ok" else "não encontrado (heurística)"}"
        RomKind.UNKNOWN ->
            "Formato não reconhecido (esperado NSP/PFS0, XCI/HEAD ou NSO0)"
    }
}

/** SHA-256 em streaming (8 MiB) — nunca carrega o arquivo inteiro em memória. */
object HashValidator {
    fun sha256(file: File, onProgress: ((bytesDone: Long, total: Long) -> Unit)? = null): String {
        val md = MessageDigest.getInstance("SHA-256")
        val buf = ByteArray(8 * 1024 * 1024)
        var done = 0L
        file.inputStream().use { ins ->
            while (true) {
                val n = ins.read(buf)
                if (n < 0) break
                md.update(buf, 0, n)
                done += n
                onProgress?.invoke(done, file.length())
            }
        }
        return md.digest().joinToString("") { "%02x".format(it) }
    }

    fun verify(file: File, expectedHex: String): Boolean =
        sha256(file) == expectedHex.lowercase()
}
