# Análise de port — MacNativeGames para Android (arm64-v8a)

Status: **Fase 1 concluída** (análise). Data: 2026-10-21.
Repositório de origem: https://github.com/ly05010419/MacNativeGames (MIT, commit `5f5de5e` analisado).
Repositório de destino: https://github.com/deivid22srk/MacNativeGames-Android

> **Aviso legal.** Este projeto não contém, não baixa e não distribui nenhum conteúdo de jogo:
> sem binários NSO/NCA, sem XCI/NSP, sem `prod.keys`, sem firmware, sem shaders compilados e
> sem texturas/áudio do jogo. Tudo que o app processa vem de arquivos que o **usuário fornece
> no próprio aparelho**, e o processamento é 100 % local. Não somos afiliados à Nintendo.
> The Legend of Zelda e Nintendo Switch são marcas da Nintendo.

---

## 1. Sumário executivo

O MacNativeGames original executa *The Legend of Zelda: Tears of the Kingdom* **nativamente** em
Apple Silicon: o código ARM64 do jogo roda direto no chip (sem emulação de CPU), um host em C
responde aos serviços do Horizon OS e a camada gráfica NVN é reconstruída sobre Metal. O
objetivo deste port é reproduzir a mesma arquitetura no Android em `arm64-v8a`, trocando a camada
Apple por equivalentes Android: Metal → **Vulkan**, AppKit/UIKit → **NativeActivity/Views**,
CoreAudio → **Oboe/AAudio**, Mach-O/codesign → **ELF/assinatura de APK**, e o pipeline de build
de desktop (`build_app.py` + hactool + reescritor Python) por um **pipeline no aparelho**.

O port é **arquiteturalmente viável** (mesma ISA ARM64, Android também é Linux, Vulkan cobre o
que o NVN precisa em princípio), mas é um projeto de **meses, não de semanas**: o reescritor de
módulos depende de decodificador AArch64 + semântica de stubs diferentes (Bionic vs Darwin), a
camada NVN→Vulkan é a maior fonte de risco de correção gráfica, e o tradutor de shaders depende
de um binário .NET NativeAOT que precisaria de um alvo `linux-bionic-arm64` (viável no .NET 8+,
mas pesado). Por isso o plano é um **MVP em marcos (M0–M5)** com critérios de aceite e testes
automatizados **sem nenhum dado do jogo** — e não uma promessa de "o jogo completo rodando".

## 2. Fontes analisadas (repositório real, commit `5f5de5e`)

Foram lidos/mapeados diretamente do repositório original:

| Arquivo / pasta | Conteúdo relevante para o port |
|---|---|
| `README.md` (533 linhas) | Escopo, requisitos (macOS 26+/Metal 4, 16 GB+), fluxo `build_app.py`, suporte iOS/iPadOS 26, controles por toque estilo Switch |
| `docs/how-it-works.md` (146 linhas) | Explicação completa: reescrita x18/tpidr/svc/cntpct, loader, host de serviços, camada NVN→Metal, tradução Maxwell→SPIR-V→MSL, layout de memória guest (<2^39), layout de texturas |
| `build_app.py` (1.350 linhas) | Pipeline completo: detecção XCI (HFS0)/NSP (PFS0), extração de ExeFS/RomFS via hactool fixado, verificação de hashes por versão (`expected-v1.0.0.json` … `v1.4.3.json`), preparação de seed de shaders, empacotamento |
| `games/totk/third_party/d2r/patch_nso.py` | O **reescritor de módulos NSO** (Python + capstone + lz4): x18→slot, tpidr_el0/ro→slots KTLS/KTLR, `svc #n`/`cntpct_el0`→portas (gates) para o host, cortes (CUTS) por build-id |
| `games/totk/runtime/*.c` (~56 arquivos C) | Host Horizon em C: `loader.c` (mapeia 3 módulos reescritos, resolve imports, inicia `nn::init::Start`), `ipc.c`/`services.c` (IPC), `fs_host.c`, `threads.c`, `audio_*.c`, `movie_host.c` (VP9), `romfs.c`, `time_host.c` |
| `games/totk/third_party/d2r_gfx/gfx/nvn/` | Camada NVN: `nvn.h`, `nvn_protos.h`, `nvn_mtl.m` (backend Metal), `thunks.S`, `entry.S`, texturas (`nvn_tex.c`, BC→ASTC via Metal) |
| `games/totk/ios/` | Alvo iOS de referência: `touch_pad.m` (controles por toque estilo Switch), `build_ipa.py`, entitlements |
| `vendor/ryujinx` + `games/totk/tools/totk_xlate` | Tradutor de shaders: Ryujinx (Maxwell→SPIR-V) + patches, compilado com **.NET NativeAOT**; binário pré-compilado `macos-arm64` com verificação SHA-256 |
| `tests/` | Testes automatizados **sem dados do jogo**: fixtures sintéticas criptografadas geradas na hora |
| `THIRD_PARTY_NOTICES.md` | Inventário de licenças: d2r-mobile (MIT), Ryujinx (MIT), bcdec, deko3d (zlib), hactool (ISC), libopus/libvpx (BSD-3), SPIRV-Cross (Apache-2.0) |

Fatos técnicos confirmados no código real (importam para o port):

- **Cabeçalho NSO** como lido por `patch_nso.py`: magic `NSO0`, flags em `0x0C`, três segmentos
  com stride `0x10` a partir de `0x10` (`{file_offset, memory_offset, decompressed_size}` + pad),
  `bss_size` em `0x3C`, build-id em `0x40..0x60`, tamanhos comprimidos em `0x60/0x64/0x68`.
- As seções text/ro/data podem estar comprimidas em **LZ4 block** (flag bit 0/1/2).
- A memória guest precisa ficar abaixo de **2^39** (IPC do SDK codifica endereços em 39 bits); o
  jogo usa **< 3 GiB** da região alias; RomFS descriptografado ~17 GB.
- O jogo depende de **threads/sincronização, FS (RomFS + saves), tempo, configurações, conta,
  input, áudio (renderer + efeitos), teclado virtual e decoder VP9** — todos implementados no
  host C e majoritariamente **portáveis** para Linux/Bionic com esforço moderado.

## 3. Mapa de dependências Apple → Android

| Apple (original) | Papel no original | Android (alvo) | Notas do port |
|---|---|---|---|
| Metal 4 / Metal 3 | Backend gráfico da camada NVN; MetalFX p/ upscale/TAA | **Vulkan 1.1+** (`libvulkan.so`, NDK) | O tradutor já produz **SPIR-V**; a etapa SPIRV-Cross→MSL simplesmente **desaparece** — vantagem estrutural do Android |
| `nvn_mtl.m` (d2r_gfx) | Implementação NVN sobre Metal | **`nvn_vulkan.c`** (nova) | Reescrever pools/render pass/barrier com semântica Vulkan; aliasing de memória (2 views do mesmo pool) via `VkBindBufferMemory`/`VK_KHR_bind_memory_2` + aliasing explícito |
| MetalFX | Upscale temporal + interpolação de frames | Sem equivalente nativo | Fase avançada: AGSL não serve (é 2D); considerar FSR1/EASU em compute Vulkan ou render nativo mais baixo + upscale bilinear |
| AppKit (launcher `.app`), UIKit (iOS) | Janela, ciclo de vida, HUD | **`Activity` + `SurfaceView`/`GameActivity`** | Sem janela flutuante; HUD como overlay de Views |
| GCController / `gamepad.m` | Controles físicos + rumble + motion | **`InputDevice` + `Vibrator` + `SensorManager`** | Mapear botões Switch para códigos Android |
| `touch_pad.m` | Controles por toque estilo Switch | **View overlay em Kotlin** (já esboçada no M0/M1) | Replicar layout duplo-stick + ABXY |
| CoreAudio / audio_renderer.c | Mixer + efeitos + saída | **Oboe** (low-latency) ou AAudio puro | O mixer/efeitos é C portátil; só a saída muda |
| libopus, libvpx (Homebrew) | Áudio Opus; filmes VP9 | Compilar via **NDK + CMake** | Ambos compilam para `android-arm64` sem patches relevantes |
| `MAP_JIT`, W^X, entitlements | Executar código gerado/reescrito | **`mmap`/`mprotect` + `memfd_create`** | Android 10+ bloqueia `execve` de arquivos não confiáveis, mas permite mapear memória anônima/fd próprio como exec. Android 15/16 endurecem RWX — ver §5 |
| APFS clonefile | Clonar 16 GB de dados sem cópia | **Cópia normal** (ou hardlink) | Custa tempo/armazenamento; 25–45 GB livres necessários |
| Mach-O + codesign | Bundle assinado verificado no boot | **ELF em memória + assinatura de APK** | O loader original já mapeia imagens cruas (`patched.bin`) — formato independente de Mach-O |
| dyld / `dlfcn` | Runtime macOS | Bionic `dlopen` (apenas para código próprio) | Código guest **não** pode virar `.so` do sistema; é mapeado manualmente como o loader original já faz |
| Mach exceptions / signals | Exceções, preempção assíncrona | **POSIX signals** (`SIGSEGV/SIGILL` + `sigaction`) | `threads.c` já usa handlers; adaptar |
| Python 3.14 + capstone + lz4 + hactool (build no Mac) | Pipeline de build | **Pipeline no aparelho** (Kotlin + C NDK) | hactool é C puro → compila no NDK; reescritor precisa de decoder AArch64 → ver §5/§6 |
| .NET NativeAOT (`totk_xlate`) | Tradutor Maxwell→SPIR-V (+MSL) | **`linux-bionic-arm64` NativeAOT** (existente no .NET 8/9) OU porta parcial | O alvo bionic existe; alternativa: integrar o tradutor como serviço nativo no processo (o modo `--service` já é um protocolo stdin/stdout) |
| SPIRV-Cross | SPIR-V → MSL | **Não necessário** | SPIR-V alimenta o Vulkan direto |

## 4. Arquitetura proposta para o Android

```
┌──────────────────────────────── app/ (APK) ────────────────────────────────┐
│  Kotlin (UI/fluxo)                    │  C/C++ (NDK, arm64-v8a)            │
│  ┌──────────────────────────────┐     │  ┌───────────────────────────────┐ │
│  │ MainActivity: importar chaves │ JNI │  │ pipeline nativo (M2+):        │ │
│  │ e ROM via SAF; progresso;     │────▶│  │ hactool-lib → ExeFS/RomFS     │ │
│  │ logs; validação M0            │     │  │ reescritor NSO (C, M2)        │ │
│  ├──────────────────────────────┤     │  ├───────────────────────────────┤ │
│  │ GameActivity: SurfaceView +   │────▶│  │ runtime host (porta de        │ │
│  │ overlay de controles Switch;  │     │  │ games/totk/runtime/*.c):      │ │
│  │ diagnóstico Vulkan (M1)       │     │  │ loader, ipc/services, fs,     │ │
│  ├──────────────────────────────┤     │  │ threads, time, audio…         │ │
│  │ BuildService: extração, hash  │     │  ├───────────────────────────────┤ │
│  │ (SHA-256), verificação por    │     │  │ nvn_vulkan.c (M3) ← SPIR-V    │ │
│  │ versão, LZ4, ELFs em memória  │     │  │ Oboe audio / libvpx (M4)      │ │
│  └──────────────────────────────┘     │  └───────────────────────────────┘ │
│  Armazenamento: app-private (filesDir) — nenhum dado sai do aparelho       │
└─────────────────────────────────────────────────────────────────────────────┘
```

Decisões estruturais desta primeira iteração:

1. **Reaproveitamento do código original**: os diretórios `games/totk/runtime/*.c` (host) e
   `d2r_gfx` são C portátil *com acoplamento Apple* em arquivos identificáveis (`.m`, `mach_*`,
   `CommonCrypto`, `libkern`). Estratégia: **não copiamos** o código acoplado ao macOS neste
   momento; reescrevemos por camadas atrás de interfaces (`platform/`), citando a origem, e o
   repositório original permanece referência e eventual submódulo. Motivo honesto: compilar o
   runtime original no NDK exigiria remover `mach_vm`/AppKit de ~15 arquivos — melhor feito
   incrementalmente por marco (M2+) do que um "vendor" quebrado no repositório.
2. **Pipeline no aparelho**: extração/verificação/reescrita rodam no device (Kotlin para
   containers e hashes; C/NDK para LZ4, reescrita e, no M2+, hactool). O que exigir .NET
   (tradutor) fica documentado: alvo `linux-bionic-arm64` do NativeAOT é o caminho planejado
   (M3+), com fallback de rodar o `totk_xlate` como processo nativo empacotado.
3. **Zero conteúdo de jogo no repositório/CI**: os testes usam **fixtures sintéticas** geradas em
   código (o original faz o mesmo: "Container tests use generated encrypted fixtures with no game
   data or real keys").

## 5. Riscos reais (honestos)

| # | Risco | Severidade | Situação e mitigação |
|---|---|---|---|
| 1 | **Página 4 K vs 16 K** | Média | Android tem 4 K na maioria dos devices; novos devices Android 15+ (e todos com `targetSdk ≥ 35` a partir de 2025-11) exigem suporte a 16 K. O loader original já lida com páginas 16 K no Darwin mantendo permissões na granularidade guest de 4 K — a mesma técnica serve. Build já alinhado: `-Wl,-z,max-page-size=16384` + NDK r27 (`ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES`) |
| 2 | **W^X / SELinux / execução de código reescrito** | Alta | O jogo reescrito é dado, não `.so`: precisa de `mmap(PROT_EXEC)` sobre memória própria. Android permite `PROT_EXEC` em memória anônima/fd do próprio app (JITs fazem isso), **mas** políticas SELinux e mudanças recentes (Android 15/16 endurecendo RWX em apps alvo 35+) podem exigir **mapeamento duplo** (`memfd_create` → um mapa RW para escrever, outro RX para executar) e `mprotect` alternado. Protótipo incluído em `platform/android` (M2 validará em device) |
| 3 | **Memória disponível** | Alta | Android reserva menos RAM por app que macOS; guest usa < 3 GiB + RomFS 17 GB em disco. Devices com 8 GB devem conseguir (o iOS exige iPhone 14+ por memória — usar o mesmo critério: 6 GB+ de RAM recomendado); risco de `lmkd` matar o app em segundo plano. Mitigação: `android:largeHeap`, executável em processo foreground-service durante o jogo |
| 4 | **Drivers Vulkan variados** | Alta | Adreno razoável, Mali/Xclipse problemáticos (barragens, sync, formats). NVN usa semânticas que o Vulkan expõe de forma diferente (barriers explícitos, aliasing). MVP isola isso atrás de `nvn_vulkan.c` e o M1 valida o caminho mínimo (swapchain+triângulo) |
| 5 | **Reescrita do código ARM64 para Linux/Bionic** | Alta | No macOS reescreve-se x18 (reservado ao kernel Darwin) e tpidr_el0 (TLS do processo). No **Bionic, tpidr_el0 é o TLS do próprio processo** → as reescritas precisam de slots próprios via memória alocada (ex.: registro por thread no host), `svc` vira `SIGILL`→gate ou patch direto, `cntpct_el0` é normalmente **truncado** em EL0 no Android → substituir por `clock_gettime(CLOCK_MONOTONIC)`. O decoder AArch64 do `patch_nso.py` (capstone) precisaria de porta C/Kotlin (opção: `vixl` ou decoder mínimo próprio — os padrões reescritos são conhecidos e finitos) |
| 6 | **Desempenho** | Alta | M4 faz ~90 FPS; GPUs mobile não têm MetalFX e têm banda de memória menor. Expectativa realista inicial: **20–30 FPS em cenários leves com resolução reduzida**, depois otimizações (cache de pipelines, subpasses, ASTC nativo em vez de BC→ASTC em compute) |
| 7 | **Tradutor de shaders no device** | Média | Caminho A: compilar `totk_xlate` para `linux-bionic-arm64` com NativeAOT (viável; .NET suporta o RID). Caminho B: tradução em C++ embutida (grande). Caminho C (ponte): traduzir no PC e importar cache — **quebra o requisito "tudo no aparelho"**; só como opção de desenvolvimento. Decisão adiada para M3 com protótipo do caminho A |
| 8 | **Armazenamento** | Média | 25–45 GB livres no device; SAF não dá caminho direto → copiar via `ContentResolver` (lento para 17 GB). Alternativa honesta: pedir persistable URI permission para ler **in place** do armazenamento do usuário (evita cópia) — implementado como opção no roadmap |
| 9 | **Chaves no aparelho** | Média | `prod.keys` fica em `filesDir` (app-private, criptografia em repouso do SO aplicável). **Nunca** logamos valores; M0 já valida presença sem imprimir |
| 10 | **Legal** | Baixa (desde que mantidas as regras) | Sem conteúdo no repo/APK/CI; licenças MIT + notices preservados; usuário fornece tudo do próprio Switch |

## 6. O que é inviável ou incerto (dito com franqueza)

- **Não é viável prometer "TotK rodando no Android" nesta primeira entrega.** O que existe hoje
  é: análise profunda do original, esqueleto Android compilável, M0 funcional (importação e
  validação estrutural **sem descriptografar** nada) e M1 (caminho Vulkan mínimo com triângulo).
- **Incerto**: fidelidade NVN→Vulkan em drivers Mali; comportamento de RWX em Android 16 com
  targetSdk 35; desempenho de GPU mobile; estabilidade de memória em devices de 6 GB.
- **Inviável** no estado atual: rodar sem o pipeline de build no aparelho completo (M2 é o marco
  que decide isso: NSO reescrito carregando até a inicialização).
- **Dependência externa forte**: obter o `totk_xlate` NativeAOT para bionic. Se falhar, M3 fica
  bloqueado por esse flanco (e o projeto não esconde isso).

## 7. MVP em marcos (critérios e testes, sem dados do jogo)

| Marco | Escopo | Critério de aceite | Testes automatizados |
|---|---|---|---|
| **M0** (implementado nesta entrega) | APK instala, UI abre, importa `prod.keys` e ROM (NSP/XCI/NSO) via SAF, valida estrutura (magics, PFS0, cabeçalho NSO, LZ4 block), calcula SHA-256, relatório no app | `assembleDebug` verde no CI; testes unitários sintéticos passando; nenhum dado do jogo no repo | `NsoParserTest`, `Pfs0ParserTest`, `Lz4Test`, `HashValidatorTest` (fixtures geradas em código) |
| **M1** (implementado nesta entrega, **não testado em hardware**) | Janela Vulkan com swapchain, loop de render e triângulo (diagnóstico) | Activity abre, triângulo renderiza em device real (pendente) | Teste unitário de carga/validação do módulo nativo limitado; verificação manual em device é o critério real |
| **M2** | Pipeline nativo: LZ4 (feito), reescritor NSO em C (decoder AArch64 dos padrões x18/tpidr/svc/cntpct), loader mapeando módulos até `nn::init::Start` | Host carrega módulos reescritos de fixtures sintéticos até logs de inicialização | Testes de reescrita com fixtures sintéticas (bytes padrão conhecidos) |
| **M3** | `nvn_vulkan.c` básico + tradução de shaders (caminho NativeAOT bionic decidido aqui) | Quadro 3D sintético renderizado pela camada NVN (sem jogo) | Probes como os do original (`probe-texture-auto-layout` portado) |
| **M4** | Áudio (Oboe), vídeo (libvpx), toque + controle físico | Áudio toca tom/stream sintético; filmes VP9 sintéticos decodificam | Testes de mixer com buffers sintéticos |
| **M5** | Memória, cache de shaders, diagnósticos | Sessão de 30 min sem OOM em device 8 GB (medição) | Benchmarks registrados em `android-port-status.md` |

## 8. Decisões legais desta primeira iteração

- `LICENSE` MIT preservado (copyright original + contribuições do port).
- `THIRD_PARTY_NOTICES.md` recriado com as licenças do original (d2r-mobile, Ryujinx, bcdec,
  deko3d, hactool, opus, libvpx, SPIRV-Cross) + novos componentes Android (Oboe/Apache-2.0 etc.
  serão adicionados quando efetivamente integrados).
- Aviso explícito de não-afiliação com Nintendo no `README.md` e na tela inicial do app.
- CI proibido de conter/baixar/exigir jogo, chaves ou firmware (o workflow só compila código).
