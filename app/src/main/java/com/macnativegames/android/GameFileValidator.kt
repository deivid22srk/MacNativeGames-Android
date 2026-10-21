package com.macnativegames.android

import java.io.InputStream

/**
 * M0 — on-device validation report. Everything in this class is safe to log or display:
 * it carries names, sizes, hashes and verdicts — never key material, never game bytes.
 */
data class ValidationReport(
    val gameFileName: String,
    val gameSizeBytes: Long,
    val container: ContainerFormat.HeaderInfo,
    val gameSha256: String,
    val keys: ProdKeysParser?,
) {
    fun toSanitizedLines(): List<String> = buildList {
        add("game: $gameFileName (${gameSizeBytes} bytes)")
        add("container: ${container.label} valid=${container.valid}")
        container.notes.forEach { add("  - $it") }
        add("sha256: $gameSha256")
        if (keys != null) {
            add("keys: ${keys.keyNames.size} names parsed, missing=${keys.missingRequired}")
        } else {
            add("keys: not provided")
        }
        add("verdict: ${verdict()}")
    }

    fun verdict(): String = when {
        !container.valid -> "FAIL: container structure rejected"
        keys != null && !keys.isValid -> "PARTIAL: container ok, keys missing ${keys.missingRequired}"
        else -> "PASS: structure accepted (on-device build pipeline is M2+ — not wired yet)"
    }
}

/**
 * M0 — the on-device pipeline entry point. This is the Android counterpart of the
 * early steps of upstream `build_app.py` (detect container by header, hash, verify
 * keys presence) and nothing more: no extraction, no game execution.
 */
object GameFileValidator {

    fun validate(game: InputStream, gameName: String, gameSize: Long, keys: InputStream?): ValidationReport {
        val container = ContainerFormat.sniff(game)
        val hash = StreamingHash.sha256(game)
        val parsedKeys = keys?.let { ProdKeysParser.parse(it) }
        return ValidationReport(gameName, gameSize, container, hash, parsedKeys)
    }
}
