# MacNativeGames-Android

**Port Android (arm64-v8a) do [MacNativeGames](https://github.com/ly05010419/MacNativeGames)** — o objetivo final é executar Tears of the Kingdom de forma **nativa** (sem emulação de CPU) em dispositivos Android arm64, do mesmo jeito que o projeto original faz no Apple silicon. **Este repositório não contém e não distribui nenhum conteúdo do jogo.**

> **Legal:** você precisa possuir o jogo. Use este projeto apenas com uma cópia sua de The Legend of Zelda: Tears of the Kingdom, com chaves e arquivos de sistema extraídos do **seu próprio** Switch. Projeto independente, sem fins comerciais. Não é afiliado à Nintendo, nem patrocinado ou endossado por ela. The Legend of Zelda e Nintendo Switch são marcas da Nintendo.

## Estado honesto

**O jogo NÃO roda ainda.** Este repositório entrega hoje:

- **M0** — app Android que abre, pede seus arquivos (XCI/NSP, `prod.keys`) via SAF e valida **localmente** estrutura e hashes (nada sai do dispositivo, nada de jogo no repo/APK/CI).
- **M1** — janela **Vulkan** com render loop e teste de triângulo (prova da pilha gráfica).
- **M2 (andaime)** — parsing de NSO, descompressão LZ4, aplicação de relocalizações RELR e registro de serviços do host Horizon, com testes automatizados sobre fixtures sintéticos (zero dados do jogo).
- **M3–M5 (andaime)** — interfaces e esqueletos para NVN→Vulkan, áudio (AAudio), vídeo (VP9), entrada estilo Switch. **Não testados em dispositivo.**

A tabela completa e honesta "funciona / não funciona / não testado" vive em [`docs/android-port-status.md`](docs/android-port-status.md).

## Como construir (CI ou local)

```sh
./gradlew assembleDebug        # APK em app/build/outputs/apk/debug/
./gradlew testDebugUnitTest    # testes de unidade (fixtures sintéticas)
cmake -S tests/native -B build-native && ctest --test-dir build-native --output-on-failure  # testes nativos (host)
```

O GitHub Actions (`.github/workflows/build.yml`) faz tudo isso e publica o APK como artifact — botão **Run workflow** (`workflow_dispatch`) ou push em `main`.

## Como usar no dispositivo

1. Instale o APK (arm64-v8a, Android 10 / API 29+).
2. No app, escolha **seus** arquivos: jogo (XCI/NSP), `prod.keys` — via seletor de arquivos do sistema.
3. O app valida estrutura e hashes **no dispositivo**. Nenhum arquivo é enviado para lugar nenhum.
4. (Futuro) O pipeline de construção do jogo roda inteiramente no dispositivo, sobre os seus arquivos.

## Documentação

- [`docs/android-port-analysis.md`](docs/android-port-analysis.md) — análise da Fase 1: mapeamento Apple→Android, riscos (16 KB pages, ABI Bionic, memória, drivers Vulkan, tradutor de shaders), marcos MVP.
- [`docs/android-port-status.md`](docs/android-port-status.md) — status por marco, atualizado a cada PR. Sem evidência, nada é "funciona".

## Licença

MIT — veja [`LICENSE`](LICENSE) e [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
