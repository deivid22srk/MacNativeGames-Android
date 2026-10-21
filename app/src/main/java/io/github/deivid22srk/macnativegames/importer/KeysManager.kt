package io.github.deivid22srk.macnativegames.importer

import java.io.File

data class KeysInfo(
    val totalKeys: Int,
    val hasHeaderKey: Boolean,
    val hasMasterKey00: Boolean,
    val hasTitlekek00: Boolean,
    val keyAreaKeyApplicationCount: Int,
)

/**
 * Gerenciador do `prod.keys` do usuário (fornecido no próprio aparelho).
 *
 * REGRA DE SEGURANÇA: valores das chaves **nunca** são logados, impressos ou
 * persistidos fora do arquivo original em armazenamento app-private.
 */
object KeysManager {

    /** Nomes mínimos que o pipeline precisa existirem (valores nunca verificados aqui). */
    val REQUIRED_KEYS = listOf(
        "header_key",
        "master_key_00",
        "titlekek_00",
        "key_area_key_application_00",
        "key_area_key_ocean_00",
        "key_area_key_system_00",
    )

    /** Formato hactool: linhas "nome = valor" (espaços opcionais); '#' inicia comentário. */
    fun parse(file: File): Map<String, String> {
        val map = LinkedHashMap<String, String>()
        file.useLines { lines ->
            for (raw in lines) {
                val line = raw.substringBefore('#').trim()
                if (line.isEmpty()) continue
                val eq = line.indexOf('=')
                if (eq <= 0) continue
                val name = line.substring(0, eq).trim()
                val value = line.substring(eq + 1).trim()
                if (name.isNotEmpty() && value.isNotEmpty() && name !in map) {
                    map[name] = value
                }
            }
        }
        return map
    }

    fun validate(map: Map<String, String>): KeysInfo = KeysInfo(
        totalKeys = map.size,
        hasHeaderKey = "header_key" in map,
        hasMasterKey00 = "master_key_00" in map,
        hasTitlekek00 = "titlekek_00" in map,
        keyAreaKeyApplicationCount = map.keys.count { it.startsWith("key_area_key_application_") },
    )

    fun describe(info: KeysInfo): String {
        val missing = REQUIRED_KEYS.filter { k ->
            when {
                k == "header_key" -> !info.hasHeaderKey
                k == "master_key_00" -> !info.hasMasterKey00
                k == "titlekek_00" -> !info.hasTitlekek00
                else -> false // key_area_key_*_00 verificadas abaixo apenas como aviso
            }
        }
        val kaWarn = if (info.keyAreaKeyApplicationCount == 0) " (sem key_area_key_application_*)" else ""
        val missingMsg = if (missing.isEmpty()) "todas as obrigatórias presentes" else "FALTAM: ${missing.joinToString()}"
        return "${info.totalKeys} chaves — $missingMsg$kaWarn"
    }
}
