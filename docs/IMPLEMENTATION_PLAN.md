# Audio Playground — Plano de Implementação

**Base:** `creative_audio_playground_development.md` (o "Guia")
**Data:** 2026-10-05
**Status:** Em execução. D1, D2 e D4 decididas em 2026-10-05 (ver §8).

---

## 0. Resumo executivo

O Guia é sólido: princípio de produto claro, não-objetivos explícitos, regras de tempo real corretas, DSP headless, schema versionado. O que falta são **decisões concretas** nos pontos onde produtos de áudio costumam quebrar: representação de tempo, como o estado atravessa a fronteira UI ↔ áudio, alinhamento de gravação, qualidade de DSP (aliasing, zipper noise, denormals) e entradas não confiáveis (arquivos de projeto e áudio importado).

As 12 mudanças mais importantes deste plano:

| # | Mudança | Pilar |
|---|---|---|
| 1 | **Uma única UI** (React + TS) rodando em WebView no desktop (JUCE 9) e no browser, em vez de duas UIs (JUCE nativa + React) | Clareza, UX |
| 2 | **Modelo de projeto e undo/redo no core C++**, como fonte única da verdade. A UI envia *intents* e recebe *patches* | Bug-free, Clareza |
| 3 | **Tempo musical em ticks inteiros** (PPQ 960) e tempo de áudio em amostras `int64`. Nunca `float` para posição | Bug-free |
| 4 | **Snapshots imutáveis do grafo** trocados de forma lock-free + fila de descarte fora da thread de áudio | Performance, Bug-free |
| 5 | **Renderizador offline no Phase 0**, como base para testes golden e para provar que offline == tempo real | Bug-free |
| 6 | **RealtimeSanitizer + ASan/UBSan/TSan + fuzzing** no CI desde o início | Bug-free, Segurança |
| 7 | **Arquivo de projeto e áudio importado tratados como entrada não confiável** (validação de schema, path traversal, limites, fuzzing de decoders) | Segurança |
| 8 | **Padrões de qualidade de DSP obrigatórios**: osciladores band-limited, smoothing de parâmetros, oversampling na distorção, filtros TPT/SVF, proteção de denormals, dither no export | Mídia |
| 9 | **Compensação de latência de gravação** para que overdubs fiquem alinhados | Mídia, Bug-free |
| 10 | **Gravação e salvamento à prova de crash** (escrita atômica, gravação incremental em disco, autosave rotativo) | Bug-free, UX |
| 11 | **"Abrir e já tocar"**: o app abre num projeto pronto, com teclado QWERTY como instrumento e captura retroativa de MIDI | UX |
| 12 | **Ordem de tarefas revisada**: modelo de projeto, undo e render offline vêm *antes* dos instrumentos, para evitar retrabalho | Clareza |

---

## 1. Análise do Guia

### 1.1 O que manter sem alterações
- Princípio *Open → Play → Record → Shape → Combine → Export* e a pergunta-guarda da §36.
- Não-objetivos (§3). Vou recusar ativamente escopo que caia ali.
- Regras de tempo real (§8) e checklist de agente (§34).
- DSP headless e IDs de parâmetro como contratos de persistência (§5, §16).
- ADRs, registro de licenças e formato de tarefa para agentes (§30–§33).

### 1.2 Lacunas e riscos encontrados

| Área | Problema no Guia | Risco | Correção proposta |
|---|---|---|---|
| Stack de UI | "Desktop shell: JUCE native" + "Web UI: React" implica **duas UIs** | Dobro de trabalho e UX divergente | §2, ADR-002 |
| Fonte da verdade | Não define onde o modelo de projeto vive (Zustand? C++?) | Estados divergentes, undo inconsistente | ADR-003 |
| Tempo | Não especifica representação de posição/duração | Drift, clipes desalinhados por ±1 amostra, bugs de snap | ADR-004 |
| Persistência | SQLite **e** `project.json` sem papel definido | Dois formatos para migrar | Remover SQLite do MVP |
| `sampleRate` no projeto | Ambíguo: taxa do motor, dos assets ou do export? | Pitch/tempo errados ao trocar de dispositivo | Assets mantêm a taxa original; motor reamostra ao carregar; `sampleRate` vira apenas o padrão de export |
| Ordem das tarefas | Modelo de projeto (008) e timeline (010) vêm depois de instrumentos e sequencer | Retrabalho ao plugar instrumentos num modelo inexistente | §4 |
| Export tardio | Render offline só na Phase 5 | Sem testes golden por 4 fases | Render offline no Phase 0 |
| Gravação | Sem compensação de latência e sem gravação segura contra crash | Overdubs fora do tempo; perda de takes | §3.6, §3.3 |
| Qualidade DSP | Não exige anti-aliasing, smoothing, denormals | Aliasing audível, cliques, picos de CPU | §3.6 |
| MIDI | Citado como motivo para desktop-first, mas ausente do MVP | Usuário com teclado MIDI fica de fora | Entrada MIDI básica no MVP |
| Segurança | §25 cobre privacidade, mas não entrada não confiável, assinatura de código nem ponte WebView | Crash ou exploit ao abrir projeto compartilhado | §3.1 |
| Licença | JUCE 9 é **AGPLv3 ou comercial** | Bloqueio legal na distribuição | ADR-001 (decidido) |
| Tempo variável | Não declara se o tempo pode mudar ao longo da música | Complexidade escondida em todo cálculo de timeline | MVP: **tempo e compasso constantes por projeto** |

---

## 2. Decisões de arquitetura (ADRs a escrever no Task 001)

### ADR-001 — JUCE 9 como core de áudio  (decidido)
- JUCE 9 é distribuído como AGPLv3 ou sob licença comercial (há um plano gratuito para receita anual pequena). Distribuir binário fechado exige licença comercial.
- **Recomendação:** manter JUCE (maturidade em dispositivos, formatos e MIDI) e registrar a licença escolhida em `docs/THIRD_PARTY_LICENSES.md` antes do primeiro release.

### ADR-002 — Uma UI: React + TypeScript em WebView
```text
┌──────────── Desktop (JUCE app) ────────────┐   ┌──────── Web ────────┐
│  WebBrowserComponent (WKWebView/WebView2)  │   │  Browser            │
│      React UI  ◄── bridge tipada ──►       │   │  React UI           │
│                    Core C++                │   │  Core C++ → WASM    │
│   (modelo, undo, engine, DSP, I/O)         │   │  em AudioWorklet    │
└────────────────────────────────────────────┘   └─────────────────────┘
```
- O JUCE 9 suporta UI em WebView com funções nativas e um provedor de recursos que serve os assets embutidos **sem servidor localhost**.
- **Ganho:** um único código de UI, um único design system, e o caminho para web vira reaproveitamento em vez de reescrita.
- **Custo:** latência da ponte (irrelevante, já que meters e playhead usam snapshots a 30–60 Hz) e a dependência do WebView2 no Windows (já vem no Windows 11).
- **Visualizações pesadas** (waveform, meters, playhead) rodam em `<canvas>`/WebGL alimentados por buffers binários, nunca pelo React re-renderizando.
- **Alternativa rejeitada:** UI nativa JUCE no desktop e React na web. São duas UIs e o UX diverge.

### ADR-003 — Core C++ é a fonte da verdade
- Modelo de projeto, comandos e undo/redo vivem no C++ (`packages/project-model` vira `core/model`).
- A UI envia **intents** tipados (`MoveClip{clipId, newStartTick}`) e recebe **patches** e eventos. O Zustand guarda só UI state e um espelho de leitura do projeto.
- Os tipos da ponte são gerados a partir de **um schema único** (JSON Schema → tipos C++ e TS), o que resolve "não duplicar definições de parâmetro" (§28).
- Undo usa comandos com `apply` e `revert` explícitos (§15). Gestos contínuos, como arrastar um knob, são **coalescidos** num único passo de undo.

### ADR-004 — Representação de tempo
- Posição e duração musical em `int64` **ticks**, com PPQ = 960. Áudio em `int64` **amostras**.
- Conversão tick ↔ amostra numa única função, testada, com tempo constante no MVP.
- Offsets dentro de assets de áudio ficam em amostras **na taxa do asset**.
- Proibido usar `float`/`double` para posição persistida.

### ADR-005 — Estado em tempo real
- **Parâmetros:** `std::atomic<float>` por parâmetro (alvo) e smoothing dentro do processador.
- **Mudanças estruturais** (adicionar track, trocar clipe, carregar sample): a thread de mensagens monta um **snapshot imutável** do grafo e publica via ponteiro atômico. O snapshot antigo vai para uma fila de descarte liberada fora da thread de áudio.
- **Eventos áudio → UI** (meters, playhead, voz ativa): FIFO SPSC lock-free lida pela UI a 30–60 Hz.
- **Gravação:** a thread de áudio escreve num ring buffer e uma thread de disco grava o arquivo.
- Nenhum mutex na thread de áudio. Isso é verificado pelo RealtimeSanitizer (§3.3).

### ADR-006 — Formato de projeto
- Pasta `MySong.playground/` contendo `project.json`, `audio/`, `cache/` (descartável) e `autosave/`.
- `project.json` com `"schemaVersion"` inteiro e migrações puras `vN → vN+1`, testadas com fixtures de todas as versões.
- Assets referenciados por **ID + hash de conteúdo**, o que permite deduplicar e oferecer relink quando o arquivo some.
- Sem SQLite no MVP. Reavaliar quando houver biblioteca global de samples.

### ADR-007 — Timeline, faixas e clipes
- Faixa de áudio com clipes de áudio; faixa de instrumento com clipes de notas, **no máximo uma por instrumento** no MVP (os parâmetros de som continuam globais).
- Clipes em ticks; o offset dentro do arquivo de áudio fica em *flicks* (exato em qualquer taxa de amostragem); clipes de notas podem repetir o conteúdo em loop.
- O padrão da bateria é um clipe de notas. Edições não destrutivas e com undo, e edições compostas viram um passo só.
- Reprodução com precisão de amostra, com fades curtos nas bordas e nos saltos (play, stop, seek, loop).

---

## 3. Melhorias por pilar

### 3.1 Segurança
**Modelo de ameaça do MVP:** o usuário abre um projeto ou arquivo de áudio recebido de terceiros, e o app roda uma WebView com ponte para código nativo.

| Controle | Detalhe |
|---|---|
| Projeto como entrada não confiável | Validação de schema estrita antes de usar qualquer valor; limites (nº de tracks, clipes, tamanho do JSON, duração); rejeitar `NaN`/`Inf` e valores fora de faixa |
| Path traversal | Caminhos de assets são relativos, normalizados e precisam ficar **dentro** da pasta do projeto. Rejeitar `..`, caminhos absolutos e symlinks que escapem dela |
| Decoders de áudio | Limites de tamanho, canais e duração; fuzzing (libFuzzer) do loader de projeto e dos decoders WAV/AIFF/FLAC no CI |
| Ponte WebView | Allowlist de comandos; todo payload validado no lado C++; CSP estrita (`default-src 'self'`); nenhuma navegação para fora do app; devtools desligado em release; nenhum conteúdo remoto |
| Privacidade (§25) | Microfone pedido só ao armar gravação; `NSMicrophoneUsageDescription` claro; nenhum conteúdo de áudio em logs; telemetria e crash reports opt-in e sem áudio |
| Distribuição | macOS: hardened runtime, assinatura e notarização. Windows: Authenticode |
| Supply chain | JUCE e dependências fixados por versão **e hash** (CMake FetchContent/CPM), lockfile do pnpm, SBOM, licenças auditadas no CI |
| Integridade de dados | Escrita atômica: `tmp` → `fsync` → `rename`, mais `.bak` da versão anterior |

### 3.2 Performance
- **Orçamento de CPU por bloco:** medir o **pior caso**, não a média. Meta: menos de 50% do tempo do bloco em 128 amostras a 48 kHz com o projeto de referência (8 tracks, 16 vozes, todos os FX).
- Benchmarks (Google Benchmark) por processador no CI, com alerta de regressão acima de 10%.
- Teste de **tamanho de bloco variável e ímpar** (1, 17, 64, 128, 333, 512, 2048). Hosts e drivers entregam blocos irregulares.
- Denormals: `juce::ScopedNoDenormals` no callback, mais DC/silence guards em feedbacks (delay, reverb).
- Vozes: limite fixo, pool pré-alocado e voice stealing com fade curto (sem clique).
- Samples: decodificados e reamostrados **fora** da thread de áudio, publicados por snapshot. No MVP ficam em memória, com limite por arquivo; streaming de disco fica para depois.
- Waveforms: **arquivos de picos multi-resolução** em `cache/`, gerados em background e desenhados em canvas.
- UI: 60 FPS; virtualização da timeline; meters fora do React state (§20); `requestAnimationFrame` lendo o último snapshot.
- Startup: abrir a UI e o projeto antes de carregar assets pesados; nada de escanear pastas no launch.

### 3.3 Bug-free (estratégia de verificação)
| Camada | Ferramenta |
|---|---|
| Segurança de tempo real | **RealtimeSanitizer** (Clang `-fsanitize=realtime`, LLVM ≥ 20) marcando o callback como `[[clang::nonblocking]]`. Falha o CI se houver alocação, lock ou syscall no caminho de áudio |
| Memória/UB/concorrência | ASan + UBSan e, em job separado, TSan |
| DSP | Golden tests com tolerância; testes de propriedade (ex.: um filtro com ganho 0 dB é transparente, a saída é sempre finita) |
| Determinismo | Renderizar o mesmo projeto 2× deve dar resultado **bit-idêntico**. Render offline e render em tempo real (simulado em blocos) devem bater dentro da tolerância |
| Matemática de timeline | Testes de propriedade (rapidcheck em C++, fast-check em TS): split + join = original; move + undo = original; snap é idempotente |
| Persistência | Round-trip save → load → save byte-estável; fixtures de todas as versões de schema; projeto corrompido nunca derruba o app |
| Fuzzing | Loader de projeto, decoders e ponte de mensagens |
| Crash safety | Gravação incremental (cabeçalho corrigido na recuperação; RF64 ou W64 acima de 4 GB); autosave rotativo (últimos 5); tela de recuperação no próximo launch |
| UI | Vitest (lógica), Playwright (fluxos E2E da UI web) |
| QA manual | Checklist de escuta por release (§22): cliques em loop points, zipper, aliasing em notas agudas, cauda de reverb no export |

### 3.4 Clareza (código e projeto)
- `AGENTS.md`/`CLAUDE.md` no Task 001, apontando para o Guia, este plano, os comandos de validação e o checklist da §34.
- Um único schema de parâmetros (`params/*.json`) gera os descritores C++, os tipos TS e a documentação.
- Estrutura do repositório, com ajustes no §6 do Guia:
```text
core/            # C++ headless: model, commands, engine, dsp, instruments, io
  model/  engine/  dsp/  instruments/  io/  bridge/
apps/desktop/    # JUCE app (janela + WebView + device manager)
apps/web/        # host web (WASM + AudioWorklet) — pós-MVP
ui/              # React + TS (compartilhado desktop/web)
schema/          # JSON Schemas: projeto, parâmetros, mensagens da ponte
presets/         # presets em JSON, versionados
tests/           # golden fixtures, integração, fuzz corpora
docs/decisions/  # ADRs
```
- Cada processador DSP segue o contrato `prepare/reset/process/setParameters`, tem um README curto com o algoritmo e as referências, e declara sua latência.

### 3.5 UI simples e elegante
- **Uma tela principal** com três zonas: *Instrumento* (pads/teclado/synth) · *Timeline* · *Painel contextual* (FX e parâmetros do item selecionado). Sem janelas flutuantes nem mixer separado no MVP: volume, pan, mute e solo ficam no cabeçalho da track.
- Controles **musicais primeiro**: "Space", "Brightness" e "Drive" na face; os parâmetros técnicos ficam num "mais" expansível.
- Design tokens (cor, espaço, tipografia, movimento) em `ui/tokens`; tema escuro como padrão e tema claro opcional; cores de meter seguras para daltonismo.
- Knobs e faders com arrasto fino (Shift), duplo clique para o valor padrão, scroll e digitação do valor; o valor aparece durante o gesto.
- Movimento mínimo e funcional, respeitando `prefers-reduced-motion`.

### 3.6 Qualidade de mídia avançada
| Tema | Requisito |
|---|---|
| Formato interno | `float32` por amostra; coeficientes e acumuladores sensíveis em `double`; estéreo explícito |
| Osciladores | Band-limited (PolyBLEP no V1); sine via tabela ou cálculo direto sem aliasing |
| Filtros | **TPT/SVF (Zavalishin)** para o filtro do synth e o LP/HP (estáveis sob modulação rápida); biquads RBJ no EQ, com coeficientes interpolados |
| Smoothing | Todo parâmetro contínuo passa por smoothing (≈ 5–20 ms) para não ter zipper noise; o tempo do delay usa interpolação para mudar sem cliques |
| Distorção | **Oversampling 4×** (polifásico) + DC blocker + compensação de ganho |
| Reverb | V1 com um algoritmo leve conhecido para destravar; V2 próprio com **FDN** (identidade sonora, §29) |
| Compressor | Detector RMS/peak com knee suave; attack/release em domínio log |
| Master | **Limiter true-peak com lookahead** (protege ouvidos e caixas); a latência do lookahead é compensada no master |
| Sampler | Interpolação de alta qualidade (Hermite no V1, sinc depois); micro-fades nos pontos start/end |
| Clipes | Fades automáticos de 1–5 ms nas bordas e crossfade no split, para nenhum corte gerar clique |
| Reamostragem | Imports com taxa diferente do motor são convertidos por um resampler de alta qualidade (r8brain-free ou libsamplerate, ambos com licença permissiva) **fora** da thread de áudio |
| Gravação | Compensação da latência de entrada + saída reportada pelo dispositivo, com ajuste manual opcional (teste de loopback depois); 24-bit/32f, na taxa do dispositivo |
| Export | WAV 16/24-bit PCM ou 32-bit float; **TPDF dither** ao reduzir para 16-bit; cauda de FX incluída (render até o silêncio, com limite); taxa configurável; medição de pico e LUFS (EBU R128) ao final |
| Formatos de import | WAV, AIFF e FLAC no MVP; MP3 e OGG depois, com a licença verificada |

### 3.7 UX excepcional
- **Tempo até o primeiro som < 5 s:** o app abre num projeto pronto (kit de bateria + synth carregados). Não há diálogo de "novo projeto" na primeira execução.
- **Teclado QWERTY como instrumento** (linha A–L = notas, Z/X = oitava) e entrada MIDI com hot-plug.
- **Captura retroativa:** o app sempre guarda o MIDI tocado recentemente, e um botão "Capturar" transforma o que você acabou de tocar em clipe. É o melhor atalho de "impulso → som" (§36).
- **Undo em tudo**, inclusive parâmetros, com Cmd/Ctrl+Z sempre funcionando.
- **Segurança auditiva:** limiter no master sempre ativo; monitoramento de entrada desligado por padrão, com aviso de microfonia sem fones.
- **Erros acionáveis (§24):** dispositivo perdido → o áudio pausa, aparece um banner com "Escolher outro dispositivo" e a UI continua navegável. Asset ausente → clipe hachurado com "Localizar…".
- **Autosave invisível**, com recuperação clara após crash.
- **Acessibilidade:** navegação 100% por teclado, labels ARIA nos controles, foco visível, contraste AA e tamanhos de alvo ≥ 32 px.
- **Onboarding:** sem tutorial bloqueante; dicas contextuais no primeiro uso e estados vazios que ensinam ("Arraste um sample aqui ou pressione R para gravar").

---

## 4. Roadmap revisado

Cada fase termina num **gate**: CI verde (sanitizers inclusos), benchmarks dentro do orçamento e o critério de aceite demonstrado.

### Phase 0 — Fundação técnica
- Repositório, CMake + JUCE fixado, pnpm workspace, CI macOS/Windows (Linux para sanitizers e fuzz)
- `AGENTS.md`, ADR-001 a ADR-007, `THIRD_PARTY_LICENSES.md`
- Device manager, callback estável, gerador de tom com smoothing
- **Renderizador offline** + harness de golden tests + RTSan
- Shell desktop com WebView exibindo a UI React "hello" via ponte tipada

**Aceite:** o app abre, toca um tom limpo, troca de dispositivo sem crash, e o CI roda os testes com sanitizers.

### Phase 1 — Core de engine e modelo
- Transport (ticks/amostras, loop, metrônomo, count-in)
- Sistema de parâmetros (schema único → C++/TS) com smoothing
- Modelo de projeto v1 + comandos + undo/redo (com coalescência)
- Snapshots imutáveis do grafo e FIFOs de eventos
- Save/Load atômico + autosave (antecipado da Phase 5)

**Aceite:** criar track, mudar parâmetro, desfazer, salvar, reabrir e obter estado idêntico (teste de round-trip).

### Phase 2 — Playground (tocar)
- Synth V1 (PolyBLEP, SVF, ADSR, 16 vozes, voice stealing sem clique)
- Sampler (start/end, pitch, gain, one-shot/gate)
- Drum machine 4×4 + sequencer de 16 steps
- Teclado QWERTY, MIDI in e captura retroativa
- UI: pads, teclado, painel do instrumento

**Aceite:** um usuário novo faz um beat e uma melodia em menos de 2 minutos sem instruções.

### Phase 3 — Gravação
- Seleção de entrada, meter, count-in, gravação incremental em disco
- Compensação de latência, overdub em layers
- Peaks em background + waveform em canvas

**Aceite:** gravar sobre um beat e ouvir o take alinhado (erro < 1 ms medido por loopback); matar o processo no meio de uma gravação não perde o áudio já capturado.

### Phase 4 — Arranjo
- Timeline (bars/beats), clipes de áudio e notas
- Selecionar, mover, trim, split (com crossfade), duplicar, deletar, loop, snap
- Tudo com undo; testes de propriedade da matemática de clipes

**Aceite:** montar uma composição curta com vários clipes, sem nenhum clique audível nas bordas.

### Phase 5 — FX próprios
- Filter → EQ → Compressor → Distortion (4× OS) → Delay → Reverb V1
- Effect chain por track (cadeia fixa da §9) + limiter true-peak no master
- Presets JSON com "macros" musicais na face

**Aceite:** golden tests de todos os processadores; o orçamento de CPU da §3.2 cumprido com o projeto de referência.

### Phase 6 — Export e polimento
- Export WAV (16/24/32f, dither, cauda, LUFS/pico); teste offline == tempo real
- Recuperação de crash, onboarding, acessibilidade, profiling
- Assinatura e notarização; reverb V2 (FDN) se houver folga

**Aceite:** a §27 do Guia (MVP Definition of Done) executada por alguém que nunca usou uma DAW.

**Pós-MVP:** Windows polish → Web (WASM + AudioWorklet; requer COOP/COEP para `SharedArrayBuffer`) → mobile.

---

## 5. Progresso (atualizado em 2026-10-06)

| Tarefa | Estado |
|---|---|
| 001–009 (Fase 0 e 1: fundação, motor, transporte, parâmetros, modelo/undo, snapshots, salvar/abrir/autosave) | ✅ Concluídas |
| 010–013 (Fase 2: DSP do synth, synth polifônico + QWERTY/MIDI, sampler + importação, bateria + sequenciador) | ✅ Concluídas |
| Verificação auditiva da Fase 2 (critério de aceite) | ⏳ Precisa de um ouvinte humano |
| 017 (timeline) | ✅ Concluída: clipes de áudio e de notas, mover/cortar/dividir/duplicar/loop, editor de notas, padrão da bateria como clipe, importação de áudio para a timeline, gravação de notas com compensação de latência, região de loop (ADR-007) |
| Verificação auditiva da timeline | ⏳ Precisa de um ouvinte humano |
| 015 (gravação de áudio) | ✅ Concluída: armar faixa de áudio (abre a entrada só então), medidor de entrada, ring lock-free → thread de disco → WAV 32f incremental, compensação de latência de ida e volta com precisão de sample, overdub em camadas, recuperação após crash (journal + reparo do cabeçalho) (ADR-008) |
| Teste de loopback da gravação (erro < 1 ms) e gravação real com microfone | ⏳ Precisa de um humano com o dispositivo |
| 014 (captura retroativa) | ✅ Concluída: o engine registra sempre as notas tocadas (relógio de samples + posição); "Capturar" (Shift+C) transforma a última frase (separada por ≥ 4 s de silêncio) em clipes — na posição da timeline se tocada com o transporte rodando, senão no compasso do playhead mantendo o ritmo |
| 016 (waveforms) | ✅ Concluída: picos de 200/s (≈ 5 ms) calculados no thread de decodificação, enviados uma vez por carregamento (base64, ≤ 10 min), canvas desenha o pico de cada coluna de pixel no zoom atual; visão geral como fallback enquanto carrega. Sem cache em disco: o áudio já é decodificado a cada abertura |
| 018 (Filter) | ✅ Concluída: SVF TPT estéreo LP/BP/HP, cutoff em escala log e Q suavizados (20 ms), troca de modo com crossfade; golden `filter_sweep_48k`. Liga na cadeia de efeitos na 024 |
| 019 (EQ) | ✅ Concluída: 3 bandas (low shelf, bell, high shelf) com as formulações SVF de Simper — mesmas curvas dos biquads RBJ, mas estáveis e sem cliques sob modulação (desvio deliberado da §3.6); golden `eq_sweep_48k` |
| 020 (Compressor) | ✅ Concluída: feed-forward estéreo-linkado, pico, joelho suave, attack/release no domínio dB (Giannoulis 2012), make-up suavizado, medidor de redução; golden `compressor_bursts_48k` |
| 021 (Distortion) | ✅ Concluída: tanh levemente assimétrico a 4× (dois estágios halfband FIR, latência inteira de 38 amostras), DC blocker, compensação de drive, tom, mix com o seco alinhado; aliasing < −70 dB até 24 dB de drive (sem OS: −16 dB); golden `distortion_sweep_48k`. A cadeia (024) compensa a latência |
| 022 (Delay) | ✅ Concluída: até 2 s, leitura Hermite com glide de 150 ms (dobra o pitch como fita, sem clique), feedback amortecido (6 kHz) e com saturação suave só na parte realimentada; golden `delay_repeats_48k`. Sync com o tempo fica para depois |
| 023 (Reverb V1) | ✅ Concluída: estrutura Freeverb (domínio público, reimplementada), 8 combs com amortecimento + 4 allpasses por canal, tamanho deslizando 300 ms, decay/damping/mix suavizados; golden `reverb_hit_48k` |
| 024 (cadeia de efeitos + limiter + presets) | ✅ Concluída (ADR-009): cadeia fixa por faixa (EQ → Comp → Filter → Dist → Delay → Reverb) com liga/desliga em crossfade, configurações no projeto (schema único → C++/TS), latência constante do motor compensada no render offline e na gravação, limiter true-peak −1 dBTP com lookahead, 18 presets JSON, painel de efeitos da faixa selecionada |
| 025 (export WAV) | ✅ Concluída: segundo motor em background configurado como o ao vivo (mesma função), render da música + cauda dos efeitos até −80 dBFS (máx. 10 s), reamostragem para 44,1/48/96 kHz, WAV 16-bit (dither TPDF) / 24-bit / 32f escrito atomicamente, LUFS integrado (EBU R128) e true peak no aviso final; teste export == tempo real com blocos irregulares |
| Fase 6 — partes técnicas | ✅ Acessibilidade (contraste AA testado, alvos de 32 px, clipes pelo teclado, nomes acessíveis testados); primeiro uso (beat pronto + dicas); "Localizar…" para áudio ausente; benchmark do projeto de referência (p99.9 ≈ 30 % do bloco de 128 a 48 kHz; meta 50 %); script de assinatura/notarização macOS (`docs/RELEASING.md`) |
| Fase 6 — precisa de humanos | ⏳ Aceite da §27 por alguém que nunca usou uma DAW; certificado Developer ID e notarização; assinatura Windows |
| Reverb V2 (FDN) | ✅ Concluída: FDN própria de 8 linhas (Hadamard, ganhos por linha para o RT60 exato de 0,3–12 s, amortecimento, difusores, modulação lenta), mesmos parâmetros do V1; RT60 medido por Schroeder (T20) dentro de 15 %; golden `reverb_hit_48k` regravado. Precisa de escuta |
| Buses de efeito (ADR-010) | ✅ Concluída: até 8 buses com cadeia, fader e pan próprios, envios por faixa pós-fader, retorno no Master; mesma latência constante do motor; testes de modelo/undo, determinismo por tamanho de bloco, cauda, export com bus, golden `bus_mix_48k` (eco + reverb num bus) e o benchmark de referência com 3 buses (p99.9 36 % do bloco, antes 29 %; meta 50 %). Sem bus solo, bus→bus nem automação de envio (de propósito) |
| Kits de bateria | ✅ Concluída: 5 kits sintetizados (Classic, 808, Lo-fi, Acoustic, Electro) escolhidos por `drums.setKit`, salvos no projeto |
| Layout redimensionável | ✅ Concluída: divisores arrastáveis com preferência guardada (`layoutPrefs.ts`) |
| Acabamento do MVP (Etapa 1) | ✅ Snap da timeline (Off, 1/4, 1/2, beat, compasso; preferência de UI), marcadores arrastáveis de início/fim do sampler (setas: 1 %, Shift: 5 %), nota de licenças para fontes/imagens/áudio. Já existiam: "Load sample…" por pad, `curve` linear/logarítmica nos parâmetros (§16), metadados do projeto do exemplo da §11 |
| Delay sincronizado (ADR-012) | ✅ Parâmetro `delay.sync` (1/16 … 1/2, tercinas e pontuadas); o motor converte nota → ms com o tempo do transporte a cada bloco; export idêntico ao tempo real; preset "Dotted eighth (follows tempo)" |
| Gestão de assets (ADR-013) | ✅ `RemoveAssets` (desfazível, recusa o que está em uso), uso derivado de clipes/pads/sampler, evento `project.assets` com "missing", menu "Audio (N)" com Locate…, Remover e "Remove unused"; os arquivos continuam em `audio/` |
| Seletor de dispositivos (ADR-014) | ✅ Menu "Audio settings" na UI (saída, entrada, taxa, buffer + latência), eventos/intents `audio.*`, entrada nunca abre pelo menu, troca recusada durante gravação, queda do dispositivo → padrão do sistema com aviso. ⏳ Desconectar o dispositivo de verdade precisa de hardware |
| Presets de synth e de bateria (ADR-015) | ✅ `presets/synth.json` (8 sons) e `presets/drums.json` (8 padrões); aplicar é um único passo de undo (`synth.setPreset`, `drums.setPattern`); testes de arquivo e de UI. Precisa de escuta para ajustar os sons |
| Testes de integração UI → core → motor | ✅ `WebUiHost` compila sem a WebView (`AP_HEADLESS_UI`) e os testes mandam intents em JSON pelo codec estrito: app.ready, intents inválidos, padrão de bateria que toca no motor, presets (um passo de undo), assets, tempo. O RTSan do CI já roda os testes que renderizam o `Engine` (a análise inicial dizia que não) |
| Escuta dos goldens de FX | ⏳ Precisa de um ouvinte humano (`tests/golden/*.wav`) |

Decisões tomadas durante a execução:
- JUCE 9.0.3, e não 8.
- Kit de bateria sintetizado por código: sem licenças de samples e funciona na primeira abertura.
- Tolerância dos goldens com funções transcendentais: 1e-4 (−80 dBFS), pela diferença medida entre a libm da Apple e a do Linux/Windows.
- Schema v1 permanece rascunho até o primeiro release (ADR-006). A 017 removeu `drums.steps` e adicionou `instrument`, `clips` e `nextClipId`.
- Lei de pan com compensação: faixa centralizada toca em ganho unitário (igual ao instrumento sem faixa).

## 5.1 Backlog inicial (substitui a §35 do Guia)

| ID | Tarefa | Depende de |
|---|---|---|
| 001 | Bootstrap: repositório, CMake + JUCE fixado com hash, pnpm, CI, `AGENTS.md`, ADRs 001–006 | — |
| 002 | Device manager + callback estável + tom com smoothing; tratamento de dispositivo perdido | 001 |
| 003 | Renderizador offline + harness golden + RTSan/ASan/UBSan no CI | 002 |
| 004 | Shell WebView + ponte tipada (schema → C++/TS) + CSP + allowlist | 001 |
| 005 | Conversão de tempo (ticks ↔ amostras) + transport + metrônomo | 003 |
| 006 | Sistema de parâmetros com schema único e smoothing | 003 |
| 007 | Modelo de projeto v1 + comandos + undo/redo | 006 |
| 008 | Snapshots de grafo + fila de descarte + FIFOs de eventos | 007 |
| 009 | Save/Load atômico, validação, path-safety, autosave + fuzz do loader | 007 |
| 010 | Oscilador PolyBLEP + SVF + ADSR (com testes DSP) | 006 |
| 011 | Synth polifônico (16 vozes, stealing) + QWERTY/MIDI | 010, 008 |
| 012 | Decoder/import seguro + resampler offline + sampler | 008, 009 |
| 013 | Drum machine 4×4 + sequencer de 16 steps | 012, 005 |
| 014 | Captura retroativa de MIDI | 011 |
| 015 | Gravação: ring buffer → disco, compensação de latência, recuperação | 008 |
| 016 | Peaks em background + waveform em canvas | 015 |
| 017 | Timeline + modelo de clipes + edições com undo | 007, 016 |
| 018–023 | Filter, EQ, Compressor, Distortion (OS), Delay, Reverb V1 | 006 |
| 024 | Effect chain + limiter true-peak no master + presets | 018–023 |
| 025 | Export WAV (dither, cauda, LUFS) + teste offline == tempo real | 003, 024 |

Cada tarefa será executada no formato da §33 do Guia, com a seção **VALIDATION** preenchida por comandos reais.

---

## 6. Definition of Done por PR

```text
[ ] Escopo = uma tarefa; sem refactors não relacionados
[ ] Format + lint (clang-format, clang-tidy, eslint, prettier) limpos
[ ] Testes unitários e DSP novos/atualizados e passando
[ ] RTSan/ASan/UBSan verdes (TSan quando tocar em concorrência)
[ ] Checklist §34 do Guia respondido no PR
[ ] Sem mudança de schema/ID de parâmetro sem migração + ADR
[ ] Benchmarks sem regressão > 10%
[ ] Nova dependência: justificada (§29) e registrada em THIRD_PARTY_LICENSES.md
[ ] UI: navegável por teclado, labels ARIA, testado em tema claro/escuro
```

---

## 7. Como vamos trabalhar juntos

1. **Uma tarefa por sessão/PR**, no formato da §33. Antes de codar, eu leio o Guia, este plano e o código existente, e confirmo o escopo.
2. **Eu rodo as validações** (build, testes, sanitizers) e relato a saída real. Se algo não puder ser executado no seu ambiente, digo exatamente o quê e por quê.
3. **Ouvido humano é insubstituível:** em tarefas de DSP eu entrego testes objetivos e um "roteiro de escuta" curto para você validar.
4. **Decisões caras viram ADR** antes do código.
5. Git: inicializar o repositório no Task 001 (hoje a pasta não é um repo git).

---

## 8. Decisões

| # | Pergunta | Minha recomendação |
|---|---|---|
| D1 | Licença JUCE: AGPLv3 (código aberto) ou comercial/Starter? | ✅ **Decidido:** licença comercial **Starter (gratuita)**; JUCE 9.0.3 em uso (ADR-001) |
| D2 | UI única React em WebView (ADR-002) ou UI nativa JUCE? | ✅ **Decidido:** React em WebView |
| D3 | Plataformas de CI: você tem máquina Windows para QA de áudio real? | macOS primeiro; Windows no CI desde o início, QA manual depois |
| D4 | Tempo e compasso constantes no MVP? | ✅ **Decidido:** fixos por projeto no MVP; edição pode vir depois |
| D5 | Distribuição inicial: só local/dev ou já assinada e notarizada? | Assinar a partir da Phase 6 |
| D6 | Formatos de import além de WAV/AIFF/FLAC no MVP? | Não |

---

## 9. Principais riscos

| Risco | Mitigação |
|---|---|
| Latência da ponte WebView afetar a sensação de "instrumento" | Toda performance (QWERTY, MIDI, pads) dispara direto no core; a UI só reflete. Medir latência tecla → som na Phase 2 (meta < 10 ms além do buffer) |
| Escopo do MVP grande para a §36 | Gates por fase; qualquer item fora da §27 vai para pós-MVP |
| Qualidade sonora "ok mas genérica" | Orçamento explícito para Reverb V2 (FDN) e tuning de presets com escuta |
| Drivers e dispositivos variados (Windows) | Matriz de teste de dispositivos; recuperação de dispositivo perdido desde o Task 002 |
| Corrupção de projeto | Escrita atômica, `.bak`, autosave rotativo, migração que nunca sobrescreve o original |
