package com.macnativegames.android

import java.io.BufferedReader
import java.io.InputStream
import java.io.InputStreamReader
import java.security.MessageDigest

/**
 * M0 — strict policy helper for `prod.keys`.
 *
 * The device may hold key material for the *user's own* console dump. Rules enforced here:
 *  - Key VALUES are parsed only to check syntax; they are never logged, never included in
 *    toString() of any report object, and never leave the process.
 *  - Only key NAMES are retained for reporting (names are public, values are not).
 *
 * Mirrors the upstream policy: "Key values are never printed."
 */
class ProdKeysParser private constructor(
    val keyNames: List<String>,
    val missingRequired: List<String>,
) {
    /** Never contains key values — safe to show/log. */
    override fun toString(): String =
        "ProdKeysParser(keyNames=${keyNames.size}, missingRequired=$missingRequired)"

    val isValid: Boolean get() = missingRequired.isEmpty()

    companion object {
        /** Required key names for the on-device pipeline (same families the upstream build needs). */
        val REQUIRED = listOf(
            "master_key_00",
            "titlekek_source",
            "header_key_source",
            "header_kek_source",
            "key_area_key_application_source",
        )

        private val LINE = Regex("^\\s*([a-z0-9_]+)\\s*=\\s*([0-9a-fA-F]+)\\s*$")

        /**
         * Parses a keys file from [input]. Returns a parser that holds only names;
         * values are validated as hex and immediately discarded.
         */
        fun parse(input: InputStream): ProdKeysParser {
            val names = linkedSetOf<String>()
            BufferedReader(InputStreamReader(input, Charsets.UTF_8)).use { r ->
                r.forEachLine { raw ->
                    val line = raw.substringBefore('#').trim()
                    if (line.isEmpty()) return@forEachLine
                    val m = LINE.matchEntire(line) ?: return@forEachLine
                    val (name, value) = m.destructured
                    // syntax check only; value is intentionally NOT stored
                    if (value.length % 2 == 0) names += name
                }
            }
            val missing = REQUIRED.filter { it !in names }
            return ProdKeysParser(names.toList(), missing)
        }
    }
}

/**
 * M0 — streaming SHA-256 over a possibly huge user file (17+ GB RomFS containers),
 * reading in fixed chunks so memory use stays flat.
 */
object StreamingHash {
    private const val CHUNK = 1024 * 1024

    fun sha256(input: InputStream): String {
        val md = MessageDigest.getInstance("SHA-256")
        val buf = ByteArray(CHUNK)
        while (true) {
            val n = input.read(buf)
            if (n < 0) break
            md.update(buf, 0, n)
        }
        return md.digest().joinToString("") { "%02x".format(it) }
    }
}
