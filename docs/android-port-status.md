# Android port status — honest table

> Regra do projeto: **nada é "funciona" sem evidência**. CI verde prova que *compila e passa nos testes de unidade* — não que roda num dispositivo. Cada linha diz o que a evidência é (ou por que não há).
> Atualizado: 2026-10-21 — **CI verde**: run [39585639631](https://github.com/deivid22srk/MacNativeGames-Android/actions/runs/39585639631) (push e `workflow_dispatch` ambos verdes; artifact `MacNativeGames-Android-apk` publicado).

## Resumo por marco

| Marco | Escopo | Status | Evidência |
|---|---|---|---|
| **M0** — casca Android + validação de arquivos do usuário | App abre, SAF pickers, sniff PFS0/HFS0/NSO0, SHA-256 streaming, parsing de `prod.keys` (nomes apenas) | ✅ **Compila + testes de unidade passam em CI** | Run 39585639631: `testDebugUnitTest` verde (12 testes JVM, fixtures sintéticas). **Não testado em dispositivo físico** |
| **M1** — janela Vulkan + triângulo | Instância/surface/device/swapchain/pipeline/render loop, SPIR-V embutido | ✅ **Compila em CI (APK gerado)** / ⚠️ **render não verificado** | `assembleDebug` verde no CI com `libmng_android.so` (1,1 MB) dentro do APK; CI não tem GPU — a prova visual do triângulo precisa de um dispositivo. Até lá: **não testado** |
| **M2** — runtime + host Horizon (andaime) | Parsing NSO0, LZ4 block decoder, RELR, registro de serviços (time/set), W^X via mmap/mprotect | ✅ **Compila + testes de host passam em CI** | Run 39585639631: `ctest` verde no runner (4 suítes gcc: nso_loader, horizon_host, audio_mixer, platform_memory) — fixtures sintéticas, zero dados de jogo |
| **M3** — NVN→Vulkan + tradução de shaders | Backend NVN real, integração `totk_xlate` | ❌ **Não iniciado** | O `totk_xlate` do upstream é macos-arm64; o alvo `linux-bionic-arm64` NativeAOT não existe ainda (ver analysis doc, risco 6) |
| **M4** — áudio/vídeo/entrada | AAudio, Opus, VP9/libvpx, layout de toque Switch, gamepad | 🟡 **Andaime parcial** | Mixer math testado no host ✓; `audio_output_android.cpp` compila mas **nunca abriu um stream em dispositivo**; libvpx ainda não está no CMake; layout de toque não implementado |
| **M5** — robustez | Memória, cache de shaders, diagnóstico 16 KB | ❌ **Não iniciado** | — |

## O que funciona hoje (com a evidência que existe)

- **APK debug builds em CI** — artifact [`MacNativeGames-Android-apk`](https://github.com/deivid22srk/MacNativeGames-Android/actions/runs/39585639631) (app-debug.apk, 12,8 MB, `arm64-v8a/libmng_android.so` 1,1 MB; auditado: **zero** `.xci/.nsp/.nca/keys/romfs/bfttf` dentro do APK).
- **16 KB page-size check** no CI: todo `.so` empacotado com LOAD alignment 0x4000 (verificado no run verde).
- **M0 validação local**: sniff de container + SHA-256 + parsing de chaves no app; valores de chaves nunca entram em logs/`toString()` (testado).
- **M2 primitivas**: header NSO0 → imagem montada (text/rodata/data, LZ4 opcional, bss zeroed); RELR address+bitmap entries; registry Horizon com `time:u`/`set:sys` despachando; mmap/mprotect com hint < 2^39 verificado no host.

## O que NÃO funciona (sem eufemismo)

- **O jogo não roda.** Não há reescritor NSO Android (x18/tp/svc/cntvct), não há loader real `main/sdk/subsdk0`, não há host Horizon de threads/FS/HID/áudio renderer, não há NVN→Vulkan.
- **Nenhum shard de NVN/Metal do upstream foi portado.** `nvn_mtl.m` (Metal/ObjC) permanece no upstream; o backend Vulkan é trabalho M3.
- **Tradutor de shaders ausente no Android** (binário macos-arm64) → sem seed, sem tradução on-device.
- **Áudio/vídeo/entrada**: nada foi ouvido/renderizado/confirmado num dispositivo.
- **MetalFX**: não existe equivalente; upscale é problema pós-M5.

## Não testado (nem positivo nem negativo)

- Qualquer dispositivo Android físico (incluindo boot do APK, permissões SAF reais, init Vulkan em Adreno/Mali/Xclipse).
- Comportamento em dispositivos de 16 KB pages (o build usa `-Wl,-z,max-page-size=16384`, mas não há probe em CI ainda).
- Desempenho (nenhuma medição; nenhuma promessa).

## Como reproduzir a evidência atual

```sh
# testes JVM (M0)
./gradlew testDebugUnitTest
# testes nativos de host (M2/M4)
cmake -S tests/native -B build-native && ctest --test-dir build-native --output-on-failure
# APK
./gradlew assembleDebug
```

## Próximos passos (ordem planejada)

1. Instalar o APK num dispositivo arm64 (M0 UI + M1 triângulo) e registrar o resultado aqui.
2. M2: reescritor NSO (port do `patch_nso.py` para C++/Bionic stubs) + loader real contra fixtures `expected-v1.0.0.json` (hashes, sem dados).
3. M3: backend NVN→Vulkan mínimo (buffers/texturas/pipelines) + investigar NativeAOT `linux-bionic-arm64` para `totk_xlate`.
4. M4: ring buffer do mixer → callback AAudio; libvpx via FetchContent; layout de toque estilo Switch.
