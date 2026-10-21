package io.github.deivid22srk.macnativegames.pipeline

import android.content.Context
import android.net.Uri
import io.github.deivid22srk.macnativegames.importer.ContainerDetector
import io.github.deivid22srk.macnativegames.importer.HashValidator
import io.github.deivid22srk.macnativegames.importer.KeysManager
import io.github.deivid22srk.macnativegames.importer.RomInfo
import org.json.JSONObject
import java.io.File

/**
 * Pipeline local do marco M0: copia os arquivos escolhidos pelo usuário (SAF) para o
 * armazenamento app-private, valida estrutura (magics/PFS0/NSO) e calcula hashes.
 *
 * Nenhum dado do jogo sai do aparelho; chaves nunca são logadas.
 * A descriptografia/extração completa fica para o M2 (hactool via NDK).
 */
class BuildPipeline(private val context: Context) {

    interface Listener {
        fun onLog(message: String)
        fun onProgress(fraction: Float)
        fun onFinished(success: Boolean, summary: String)
    }

    var keysFile: File? = null
    var romFile: File? = null

    fun importsDir(): File = File(context.filesDir, "imports").apply { mkdirs() }

    /** Copia o Uri (SAF) para o armazenamento privado do app. */
    fun importFromUri(uri: Uri, kindLabel: String): File {
        val resolver = context.contentResolver
        var name = queryDisplayName(uri) ?: ("import-${System.currentTimeMillis()}.bin")
        name = name.replace(Regex("[^A-Za-z0-9._-]"), "_")
        val dir = File(importsDir(), kindLabel).apply { mkdirs() }
        val dst = File(dir, name)
        val total = (resolver.openAssetFileDescriptor(uri)?.length ?: -1).coerceAtLeast(0)
        resolver.openInputStream(uri)!!.use { ins ->
            dst.outputStream().use { outs ->
                val buf = ByteArray(1024 * 1024)
                var copied = 0L
                while (true) {
                    val n = ins.read(buf)
                    if (n < 0) break
                    outs.write(buf, 0, n)
                    copied += n
                    if (total > 0) Listener_progress(copied.toFloat() / total)
                }
            }
        }
        return dst
    }

    private fun Listener_progress(f: Float) {
        progressCb?.invoke(f)
    }

    private var progressCb: ((Float) -> Unit)? = null

    fun setProgressCallback(cb: ((Float) -> Unit)?) {
        progressCb = cb
    }

    private fun queryDisplayName(uri: Uri): String? = try {
        context.contentResolver.query(
            uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null
        )?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        }
    } catch (_: Exception) {
        null
    }

    data class M0Report(
        val romInfo: RomInfo,
        val romSha256: String,
        val keysDescription: String,
    )

    /**
     * Executa a validação M0 sobre os arquivos importados.
     * Nunca lança para falhas "esperadas" — reporta via [Listener].
     */
    fun runM0(listener: Listener) {
        try {
            val keys = keysFile
            val rom = romFile
            if (keys == null || !keys.isFile) {
                listener.onFinished(false, "Importe o prod.keys primeiro (passo 1).")
                return
            }
            if (rom == null || !rom.isFile) {
                listener.onFinished(false, "Importe a ROM (NSP/XCI) ou NSO primeiro (passo 2).")
                return
            }

            listener.onProgress(0.02f)
            listener.onLog("Validando chaves (valores nunca são exibidos)…")
            val keysMap = KeysManager.parse(keys)
            val keysInfo = KeysManager.validate(keysMap)
            listener.onLog("Chaves: ${KeysManager.describe(keysInfo)}")
            listener.onProgress(0.08f)

            listener.onLog("Detectando estrutura do arquivo (${formatSize(rom.length())})…")
            val romInfo = ContainerDetector.detect(rom)
            listener.onLog("Estrutura: ${ContainerDetector.describe(romInfo)}")
            if (romInfo.kind == io.github.deivid22srk.macnativegames.importer.RomKind.UNKNOWN) {
                listener.onFinished(false, "Arquivo não reconhecido como NSP/XCI/NSO. Nada foi alterado.")
                return
            }

            listener.onLog("Calculando SHA-256 (streaming)…")
            val sha = HashValidator.sha256(rom) { done, total ->
                if (total > 0) listener.onProgress(0.1f + 0.85f * (done.toFloat() / total))
            }
            listener.onLog("SHA-256: ${sha.take(16)}…")
            listener.onProgress(0.97f)

            persistManifest(romInfo, sha, keysInfo.totalKeys)

            val okKeys = keysInfo.hasHeaderKey && keysInfo.hasMasterKey00
            val summary = buildString {
                append(if (okKeys) "M0 concluído" else "M0 parcial")
                append(": estrutura válida (")
                append(romInfo.kind.name)
                append("), chaves ")
                append(if (okKeys) "OK" else "incompletas")
                append(". Nada foi descriptografado (extração real no M2).")
            }
            listener.onProgress(1f)
            listener.onFinished(true, summary)
        } catch (e: Exception) {
            listener.onFinished(false, "Falha na validação: ${e.message}")
        }
    }

    private fun persistManifest(romInfo: RomInfo, sha: String, keysCount: Int) {
        val manifest = JSONObject().apply {
            put("timestamp", System.currentTimeMillis())
            put("romKind", romInfo.kind.name)
            put("romSize", romInfo.sizeBytes)
            put("romSha256", sha)
            put("keysCount", keysCount)
            put("pipeline", "M0")
        }
        File(context.filesDir, "import_manifest.json").writeText(manifest.toString())
    }

    private fun formatSize(bytes: Long): String = when {
        bytes >= 1L shl 30 -> "%.2f GB".format(bytes.toDouble() / (1L shl 30))
        bytes >= 1L shl 20 -> "%.1f MB".format(bytes.toDouble() / (1L shl 20))
        else -> "$bytes B"
    }
}
