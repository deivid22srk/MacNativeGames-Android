# Análise de portabilidade: MacNativeGames (Apple silicon) → Android (arm64-v8a)

> Documento da Fase 1 do port. Última atualização: 2026-10-21.
> Repositório de referência analisado: https://github.com/ly05010419/MacNativeGames (MIT), commit `1eff989`.
> Este documento é uma análise honesta de engenharia. Não afirma nada que não tenha sido verificado no código-fonte do projeto original.

---

## 1. Resumo executivo

O MacNativeGames **não é um emulador de CPU**: o código ARM64 do jogo (Tears of the Kingdom / Breath of the Wild) executa diretamente no chip Apple, reescrito pontualmente por um reescritor de módulos NSO, enquanto um host em C responde pelas chamadas de sistema e serviços IPC do Horizon (o SO do Switch), e uma camada NVN→Metal reconstrói a pilha gráfica sobre o Metal 4. O mesmo princípio **é tecnicamente aplicável ao Android em dispositivos arm64-v8a**: Android também roda código ARM64 nativo, também reserva o registrador de plataforma `x18`, expõe `mmap`/`mprotect` para memória executável anônima e tem Vulkan 1.1+ na grande maioria dos GPUs móveis modernos.

Porém, o port não é mecânico. As dependências de macOS estão enraizadas em três pontos: (1) os stubs de syscall/asm escritos para Mach-O (`gate.S`, `thread_enter.S`, `nvn_thunks.S`), (2) a camada gráfica NVN→Metal em Objective-C (`nvn_mtl.m`, 2,4 MB de código NVN) e (3) o tradutor de shaders pré-compilado `totk_xlate`, distribuído apenas como binário NativeAOT **macos-arm64**. A estratégia adotada neste port é isolar tudo isso atrás de uma fronteira de plataforma (`platform/{macos,ios,android}`) e reconstruir o caminho crítico para Android: NSO→ELF na memória, host Horizon em C portável, NVN→Vulkan, áudio AAudio/Oboe, entrada via `InputDevice`/`GameActivity`.

**Escopo honesto deste repositório:** ele entrega o esqueleto Android completo (app Kotlin + camada nativa CMake), o marco M0 (validação local de arquivos do usuário, zero conteúdo de jogo) e o marco M1 (janela Vulkan com render loop e teste de triângulo) compilando em CI, além do andaime testável dos marcos M2–M5. Ele **não entrega o jogo rodando** — isso exigiria o port completo do reescritor NSO, do host Horizon e do backend NVN→Vulkan, item a item verificado contra o hardware real. A regra do projeto é: nada é declarado "funciona" sem evidência.

---

## 2. O que o projeto original faz (como lido do código)

Componentes confirmados na leitura de `README.md`, `docs/how-it-works.md`, `build_app.py` e `games/totk/`:

| Componente | Onde | Papel |
|---|---|---|
| Reescritor NSO | `games/totk/third_party/d2r` (`patch_nso.py`, `nsoimg.py`, `relr.py`) | Reescreve x18, ponteiro de thread, `svc`, registradores de contador → saltos para stubs do host; verifica byte a byte contra `assets/expected-v*.json` |
| Loader | `games/totk/runtime/loader.c` | Mapeia `main`, `sdk`, `subsdk0` no layout original, resolve imports, inicia `nn::init::Start` |
| Host Horizon | `games/totk/runtime/*.c` (~900 KB) | Threads/sincronização, memória, FS/RomFS, tempo, settings, conta, input, áudio (renderer + efeitos portados de Ryujinx.Audio), teclado swkbd, decoder VP9 via libvpx |
| Camada gráfica | `games/totk/third_party/d2r_gfx/gfx/nvn/` (~2,4 MB) | Implementa a API NVN do jogo e reprocessa os command buffers no Metal 4: pools→recursos, render passes, draws; shaders Maxwell→SPIR-V (Ryujinx)→MSL (SPIRV-Cross), com cache e "seed" de shaders de inicialização |
| Tradutor de shaders | `games/totk/tools/totk_xlate` + `vendor/ryujinx` | Programa nativo único (Ryujinx + patches) compilado com .NET NativeAOT; distribuído pronto apenas para **macos-arm64** (`prebuilt/macos-arm64`, SHA-256 fixado) |
| Áudio | `runtime/audio_{mixer,effects,reverb,renderer}.c` + Opus | Mixer em C portável; saída via Core Audio |
| Vídeo | `runtime/movie_host.c` | VP9 via libvpx |
| Build | `build_app.py` (1.350 linhas) | Detecta XCI/NSP, extrai/verifica ExeFS e RomFS (6 níveis IVFC), gera overlays, semeia shaders, empacota `.app`/`.ipa`; `--platform macos|ios` |
| iOS | `games/totk/ios/` (xcodegen) | Referência direta de port para outro SO: main.m, touch_pad, entitlements de memória aumentada; 1.4.3 já rodou em iPhone 14/iPad |

Fatos de runtime relevantes para Android (de `docs/how-it-works.md`):

- **Memória do guest deve ficar abaixo de 2^39** (buffers IPC codificam endereços em 39 bits). O host reserva região alias de 64 GiB em `0x70_0000_0000` + ~6 GiB de stack/heap; onde o layout fixo não cabe, empilha tudo lado a lado num buraco livre e encolhe o alias em passos de 4 GiB até 8 GiB (o jogo usa < 3 GiB do alias).
- **O jogo consome ~4 GB de RAM** no iPhone 14 e o iOS às vezes encerra por memória — no Android o risco é análogo e depende do limite por app de cada fabricante.
- **Sem seed de shaders no dispositivo, draws sem variante são pulados** (comportamento já aceito no iOS) — objetos podem faltar em cenas nunca reportadas por outros jogadores.

---

## 3. Tabela de mapeamento de dependências Apple → Android

| Dependência Apple (uso confirmado no código) | Equivalente Android | Observações de port |
|---|---|---|
| Metal 4 (`.app` macOS 26+) | **Vulkan 1.1+** (NDK `libvulkan.so`) | SPIR-V produzido pelo tradutor Ryujinx é consumido **direto** pelo Vulkan — a etapa SPIRV-Cross→MSL simplesmente desaparece no Android. Sem MetalFX: upscale/interpolação ficam fora do MVP (documentado) |
| NVN→Metal (`nvn_mtl.m`, Objective-C) | **NVN→Vulkan** (C puro + Vulkan) | Reescrita nova; `nvn_hle.c`/estruturas e a lógica de pools/texturas (incl. correção de block height H2) são portáveis |
| Mach-O + assembly Mach-O (`gate.S`, `thread_enter.S`, `nvn_thunks.S`, `nvn_entry.S`) | **ELF + assembly GNU/as para Bionic** | Mesma ISN ARM64, sintaxe de relocalização/diretivas diferente; `x18` também é reservado no Android — a categoria de reescrita coincide |
| MAP_JIT + entitlement W^X | **`mmap`(PROT_WRITE) + `mprotect`(PROT_EXEC)** em memória anônima | Domínio SELinux `untrusted_app` permite execmem anônima (JITs como V8/Mono dependem disso); `targetSdk 29+` proíbe exec de arquivo gravável do app — o loader usa memória anônima, então não é afetado |
| `svc` → stubs Mach | **`svc` → stubs Bionic/libc** | As chamadas reescritas passam a chamar as mesmas primitivas via APIs libc do Bionic (`clock_gettime`, `membarrier`, etc.) |
| Core Audio (`audio_renderer.c`→CoreAudio) | **AAudio (NDK) / Oboe** | Mixer/efeitos já são C portável (portados de Ryujinx.Audio); só a saída muda |
| Homebrew `opus`, `libvpx` | **Opus/libvpx compilados pelo CMake do NDK** | Fontes pinned via FetchContent no CI; sem binários pré-compilados no repo |
| GCController / AppKit (`gamepad.m`, `frontend.m`) | **`InputDevice` + `GameActivity`/`NativeActivity`** | Mapeamento Switch Pro por posição (PlayStation/Xbox) ou por rótulo (Nintendo), igual ao Mac; vibração via `Vibrator`/HDL, giroscópio via `TYPE_GYROSCOPE` |
| UIKit/iOS `touch_pad.m` | **SurfaceView overlay estilo Switch** (dpad + botões + stick esquerdo) | O layout de toque do iOS serve de referência direta |
| APFS clonefile (~25–45 GB de work folder) | **Cópia ordinária** — sem equivalente | Android não tem clonefile; o pipeline no dispositivo precisa de ~2× RomFS temporário ou extração streaming direto para o armazenamento do app; SDcard SAF é lento para 17 GB — usar `getFilesDir()`/`getExternalFilesDir()` |
| `.NET NativeAOT` `totk_xlate` (macos-arm64) | **`linux-bionic-arm64` NativeAOT** (rebuild) — **não incluído no MVP** | .NET 8+ suporta alvo `linux-bionic-arm64`; até isso existir, o Android se comporta como o iOS sem seed: draws sem shader são pulados. Alternativa futura: tradução em servidor e cache importado via SAF |
| xcodegen (iOS) / AppKit launcher | **Gradle Kotlin DSL** (AGP) | `minSdk 29`, `arm64-v8a` único; UI em Kotlin |
| codesign/notarização | **APK Signature Scheme v2/v3** (`apksigner`) | Sem notarização; debug-signed no CI, release assinado só se keystore secret existir |
| `mach_vm_remap` (alias de memória) | **`memfd_create` + `mmap` MAP_SHARED repetido** | Aliasing de pools de memória do guest resolve-se com memfd mapeado duas vezes; probes de copy-alias precisam de revalidação no Adreno/Mali |
| Páginas de 4 KB (macOS/iOS) | **4 KB e 16 KB** (Android 15+ em alguns dispositivos) | Linkar tudo com `-Wl,-z,max-page-size=16384` (NDK r27+) para funcionar nos dois |

---

## 4. Riscos honestos (sem maquiagem)

1. **Tamanho de página 16 KB.** Dispositivos Android 15+ com páginas de 16 KB exigem que todo `.so` tenha segmentos alinhados. Mitigação já aplicada: `max-page-size=16384` no CMake/NDK. Residual: bibliotecas de terceiros futuras (Opus/libvpx) precisam da mesma flag — verificação automatizada planejada no CI (M5).
2. **ABI: o código do jogo foi reescrito para Mach-O, não para Bionic.** As categorias de reescrita (x18, tp, svc, cntvct) coincidem com o que o Android proíbe/exige, mas cada site reescrito precisa ser reverificado contra o carregador Android. O verificador do upstream (`expected-v*.json`) continua válido e é reaproveitado como âncora de integridade.
3. **Espaço de endereçamento.** O Android arm64 clássico dá 512 GiB de VA de usuário (39 bits) — o layout `< 2^39` encaixa por construção. Porém kernels com 52-bit VA (`CONFIG_ARM64_VA_BITS=52`) podem posicionar o binário acima de 2^39; o loader precisa do modo "lado a lado num buraco livre" do host (já implementado no upstream para macOS 27/iOS) e `mmap` com hint fixo + fallback. **Não testado** — é um dos primeiros alvos do M2.
4. **Memória disponível.** O jogo quer ~4 GB; `android:largeHeap` não garante isso em todos os fabricantes. Dispositivos com 12–16 GB tendem a ceder mais; APKs com `android:extractNativeLibs=false` reduzem cópia. Risco declarado, sem solução universal — o status doc separará "roda em dispositivo X" de "funciona".
5. **Fragmentação de drivers Vulkan.** Adreno/Mali/Xclipse diferem em barreiras, aliasing e clear values (os probes D16 do upstream mostram sensibilidade a conversão de depth clears por GPU). Cada probe do upstream precisará ser reproduzido em Vulkan por família de GPU. Nada disso está pronto — M3/M4.
6. **Tradutor de shaders ausente no Android (MVP).** `totk_xlate` é macos-arm64. Sem ele, on-device não há seed nem tradução em runtime → comportamento "draw pulado" (idêntico ao iOS sem cache). Documentado em vez de escondido; caminho de solução: NativeAOT `linux-bionic-arm64` ou import de cache via SAF.
7. **Performance desconhecida.** 90–120 FPS no Mac M4 não diz nada sobre um Snapdragon 8 Gen 2 com Adreno 740. Nenhuma promessa de FPS é feita neste repositório.
8. **Legal.** Zero conteúdo de jogo no repo/APK/CI: sem XCI/NSP, sem prod.keys, sem firmware, sem RomFS, sem shaders compilados, sem texturas/áudio. O pipeline roda **no dispositivo** sobre arquivos escolhidos pelo usuário via SAF. Logs nunca imprimem material-chave; CI nunca recebe segredo de jogo. MIT + `THIRD_PARTY_NOTICES.md` preservados; projeto não afiliado à Nintendo.

---

## 5. Marcos MVP (M0–M5) — o que cada um entrega e o que NÃO afirma

| Marco | Entrega | Critério de aceite | Não afirma |
|---|---|---|---|
| **M0 — Casca Android + validação** | App abre, pede XCI/NSP e prod.keys via SAF, valida estrutura (HFS0/PFS0, NSO, ticket) e calcula SHA-256 localmente | Testes de unidade com fixtures sintéticos (bytes gerados, zero jogo) passam em CI | Nada executa código do jogo |
| **M1 — Vulkan** | Janela Vulkan (surface da Activity), swapchain, render loop, triângulo colorido | APK no artifact do CI; prova de render é manual no dispositivo (CI não tem GPU) | Não renderiza nada do jogo |
| **M2 — Runtime + host Horizon** | Andaime do host: registro de serviços, plumbing de IPC, loader NSO→memória anônima (parsing + layout) compilando e testável com fixtures | Testes nativos de parsing/loader (sem dados reais) verdes | Não carrega módulos reais do jogo |
| **M3 — NVN→Vulkan + tradução** | Backend NVN→Vulkan básico (buffers, pipelines, barreiras) + integração do tradutor (quando `linux-bionic` existir) | Probes de swapchain/pipeline sintéticos | Não renderiza cenas do jogo |
| **M4 — Áudio, vídeo, entrada** | AAudio output, Opus decode, VP9 via libvpx, layout de toque Switch + gamepad físico | Testes de mixer matemático; input manual | Não promete áudio/vídeo do jogo |
| **M5 — Robustez** | Otimização de memória, cache de shaders, diagnóstico, probe de 16 KB no CI | Logs honestos no `android-port-status.md` | Não afirma estabilidade de longa sessão |

`docs/android-port-status.md` mantém a tabela viva "funciona / não funciona / não testado" por marco, atualizada a cada PR.

---

## 6. Estratégia de reuso do código original

- O upstream fica **referenciado** (SHA `1eff989`) e/ou como submodule — nada de copiar em massa código que ainda não compila para Android; o port é file-by-file com header de atribuição.
- `platform/macos`, `platform/ios` permanecem no upstream; aqui criamos `platform/android` atrás das mesmas interfaces (`runtime.h`).
- Partes já portáveis (mixer de áudio, parsing NSO, verificação de hashes) entram primeiro; assembly Mach-O e ObjC/Metal ficam de fora até seus substitutos Bionic/Vulkan existirem.
- `LICENSE` (MIT, copyright upstream) + `THIRD_PARTY_NOTICES.md` preservados e estendidos a este port.

## 7. Decisão final da Fase 1

Prosseguir com o esqueleto Android (Fase 2) focando em **M0 e M1 reais e compilando em CI**, com M2–M5 como andaime honesto e testável — em vez de prometer o jogo inteiro. Os riscos 3, 5, 6 e 7 são bloqueadores conhecidos para "jogo rodando" e ficam registrados no status doc até haver evidência em contrário.
