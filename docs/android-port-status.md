# Android port status — honest table

> Regra do projeto: **nada é "funciona" sem evidência**. CI verde prova que *compila e passa nos testes de unidade* — não que roda num dispositivo. Cada linha diz o que a evidência é (ou por que não há).
> Atualizado: 2026-10-21 (sessão 2) — correções M0/M1 pós-teste em dispositivo real (Motorola Edge 30 Fusion, Adreno, Android 14) + M3/M4/M5 implementados como probes verificáveis on-device.

## Correções desta sessão (a partir de feedback real de dispositivo)

| Problema reportado | Causa raiz | Correção | Evidência |
|---|---|---|---|
| "Validate locally" congelava o app (logcat: `Signal Catcher reacting to signal 3` = ANR) | Validação (SHA-256 streaming de GBs) rodava na **UI thread** | Validação movida para thread em background; UI recebe o relatório via `runOnUiThread`; botão protegido contra reentrada | Revisão de código + compilado; ANR só se reproduziria com arquivo grande — **aguardando re-teste no dispositivo** |
| M1 mostrava só azul liso, sem triângulo | O shader usava o triângulo *fullscreen oversized* `(-1,-1),(3,-1),(-1,3)` com cor azul `rgb(30,140,219)` — cobria a tela inteira por design | Shaders regenerados: triângulo pequeno centrado com gradiente RGB por vértice sobre fundo escuro; `scripts/emit_shaders.py` versionado (SPIR-V validado: magic/version/word-count) | SPIR-V verificado por script (magic `0x07230203`); prova visual **aguardando re-teste no dispositivo** |
| Risco latente: race de resize | `surfaceChanged` chamava resize nativo da UI thread durante submits | Resize roteado por flag atômica consumida no render thread; `render_frame` trata `VK_SUBOPTIMAL_KHR`/`OUT_OF_DATE`; command buffers realocados pós-resize | Compila; comportamento em rotação **não testado** |

## Resumo por marco

| Marco | Escopo | Status | Evidência |
|---|---|---|---|
| **M0** — casca Android + validação de arquivos do usuário | App abre, SAF pickers, sniff PFS0/HFS0/NSO0, SHA-256 streaming, parsing de `prod.keys` (nomes apenas) | ✅ **Testado em dispositivo real + testes JVM verdes** | Usuário executou o APK num Edge 30 Fusion: app abre, arquivos selecionam, validação roda (agora em background); 12 testes JVM em CI |
| **M1** — janela Vulkan + triângulo | Instância/surface/device/swapchain/pipeline/render loop, SPIR-V embutido | 🟡 **Render confirmado em dispositivo; triângulo regenerado — re-teste pendente** | Logcat do dispositivo prova init/present a 59 fps; contador de frames nativo correu 218+ frames. O triângulo VISÍVEL depende do novo SPIR-V (não havia triângulo — era fullscreen azul) |
| **M2** — runtime + host Horizon (andaime) | Parsing NSO0, LZ4, RELR, registro de serviços (time/set), W^X mmap/mprotect | ✅ **Testes de host verdes em CI** (fixtures sintéticas) | `ctest` verde no runner + local (4 suítes); **nenhum módulo de jogo real carregado ainda** |
| **M3** — NVN→Vulkan | Shim com **141 entry points** registrados (mesma via do upstream: `nvnBootstrapLoader` + `nvnDeviceGetProcAddress`), objetos (device/queue/window/pool/texture/sampler/shader/pipeline/cmdbuffer), replay de comandos (clear→bind→viewport→draw) em VkCommandBuffer real, present por swapchain próprio; **probe M3** desenha o triângulo resolvendo cada call POR NOME do registry | 🟡 **Implementado + compilado; probe on-device pendente** | `llvm-nm`: `nvnBootstrapLoader` exportado; build verde com os 5 .cpp do shim; contadores honestos (`counters()`) expostos na tela de probe; **0 texturas de jogo criadas** — path real depende de M2 carregar módulos |
| **M4** — áudio + entrada | AAudio real (stream + callback puxando do mixer `mix_stereo_s16`, tom 440 Hz de teste); `input_manager` puro (toque + keycodes Android→layout Switch A/B/X/Y/L/R/ZL/ZR/+/-/D-pad + stick analógico); telas de probe no app | 🟡 **Implementado + testes de host verdes; som/toque em device pendentes** | `test_audio_mixer` + `test_input_manager` passam; AAudio compila contra NDK 27.2 e é exercido pelo botão "Play 440 Hz tone" — **nunca ouvido em dispositivo ainda** |
| **M5** — robustez | Cache de shaders SPIR-V em disco (SHA-256 próprio, **validado contra vetor de teste conhecido**), ring de diagnóstico (256 eventos) + `diagReport()`, budget de memória (meminfo → cap 4 GiB), 16 KB pages mantidos | ✅ **Testes de host verdes** / 🟡 device pendente | `test_shader_cache` (SHA-256 known-answer + round-trip + eviction), `test_diag` (budget math + ring cap) passam local e devem passar no CI |

## O que funciona hoje (com a evidência que existe)

- **APK debug builds em CI** — artifact `MacNativeGames-Android-apk`; APK anterior auditado: **zero** `.xci/.nsp/.nca/keys/romfs/bfttf` (o novo APK mantém a mesma regra: nenhum dado de jogo entra no repo/CI).
- **Em dispositivo real (reportado pelo usuário)**: app abre, M1 roda a 59 fps com contador de frames nativo (prova init Vulkan + swapchain + present), SAF opera.
- **16 KB page-size check** no CI.
- **Testes**: 12 JVM (M0) + 7 suítes nativas de host (nso_loader, horizon_host, audio_mixer, platform_memory, shader_cache, input_manager, diag) — todas verdes localmente; CI valida no runner.

## O que NÃO funciona (sem eufemismo)

- **O jogo não roda.** Falta: reescritor NSO Android (x18/tp/svc/cntvct), loader real `main/sdk/subsdk0` com as keys do usuário, host Horizon de threads/FS/HID/áudio-renderer completo, integração do shim M3 com módulos carregados pelo M2.
- **Tradutor de shaders `totk_xlate` ainda é binário macos-arm64** — o shim M3 aceita SPIR-V pronto (o probe usa os shaders M1 embutidos como stand-in), mas não há tradução on-device ainda (NativeAOT `linux-bionic-arm64` é a rota, ver analysis doc, risco 6).
- **Descriptor binding, index buffers, compute, sync** no shim M3: registrados, chamáveis, **contam como "não implementado"** e são pulados no replay — nada finge sucesso.
- **Vídeo (libvpx)**: continua stub; não entrou no CMake.
- **MetalFX**: sem equivalente.

## Não testado (nem positivo nem negativo)

- O novo triângulo M1 e o probe M3 **no dispositivo** (o build desta sessão ainda não chegou lá — baixe o APK mais recente do CI).
- Tom AAudio em alto-falante real; gamepad Bluetooth real; rotação de tela com o novo caminho de resize.
- Desempenho (nenhuma medição; nenhuma promessa).

## Como reproduzir a evidência atual

```sh
# testes JVM (M0)
./gradlew testDebugUnitTest
# testes nativos de host (M2/M3/M4/M5)
cmake -S tests/native -B build-native && ctest --test-dir build-native --output-on-failure
# APK
./gradlew assembleDebug
# probes no app: botões "NVN→Vulkan probe (M3)", "Audio + input probe (M4)", "Diagnostics (M5)"
```

## Próximos passos (ordem planejada)

1. Instalar o APK novo e registrar aqui: triângulo M1 visível, contadores do probe M3, tom M4, relatório M5.
2. M2→M3 ponte: carregar NSO real (com keys do usuário via SAF) até `nvnDeviceInitialize` do shim e registrar o que falta no caminho.
3. `totk_xlate` NativeAOT `linux-bionic-arm64` (ou rodar o tradutor no PC e injetar SPIR-V via cache M5 — rota mais curta já suportada pelo `ShaderCache`).
4. Descriptor sets + index buffers no replay do shim (primeiros draws não-triviais).
