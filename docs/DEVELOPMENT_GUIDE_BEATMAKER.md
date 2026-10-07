# MusicBox — Beatmaker First Experience

Implemente a primeira experiência do modo **Beatmaker** do MusicBox.

O objetivo não é criar uma drum machine, um sequenciador completo ou uma mini-DAW. Esta etapa deve implementar um **instrumento sonoro extremamente imediato**, no qual o usuário abre a experiência, ouve um loop interessante e começa a moldá-lo através de um único gesto principal.

## Princípio central

> **Sound → Gesture → Immediate musical response**

O usuário deve começar criando antes de precisar entender a interface.

A experiência deve transmitir:

- exploração;
- curiosidade;
- resposta tátil;
- simplicidade;
- qualidade sonora;
- controle;
- sensação de instrumento físico.

Evitar qualquer sensação de painel técnico de produção musical.

---

# 1. Persona

A experiência é destinada à persona **The Beatmaker**.

É alguém que frequentemente não começa com uma composição pronta na cabeça.

Seu processo é:

```text
Hear
 ↓
Touch
 ↓
Change
 ↓
React
 ↓
Explore
 ↓
Discover an idea
```

Para essa persona:

> **Explorar o som já é compor.**

O primeiro minuto deve demonstrar isso sem tutorial.

---

# 2. Experiência inicial

Ao entrar no Beatmaker:

```text
Open Beatmaker
      ↓
Loop is ready
      ↓
Play
      ↓
Analog sequence starts
      ↓
Turn the main knob
      ↓
Sound changes immediately
      ↓
Keep exploring
```

Não apresentar inicialmente:

- browser de presets;
- tracks;
- mixer;
- routing;
- plugins;
- piano roll;
- inspector;
- menus técnicos;
- parâmetros avançados.

A tela inicial deve parecer um **instrumento**, não um software de produção.

---

# 3. Som inicial

O primeiro som será uma:

> **sequência de sintetizador analógico, hipnótica, minimalista e chill-out.**

Características:

```text
Length: minimum 4 bars
Tempo target: ~90 BPM
Feel: relaxed
Character: electronic / analog
Energy: low–medium
Density: sparse
Movement: hypnotic
Repetition: intentional
```

A sequência deve possuir poucas notas e espaço entre os eventos.

Evitar:

- leads agressivos;
- EDM;
- arpejos excessivamente rápidos;
- excesso de notas;
- timbres brilhantes demais;
- progressões que imponham uma música específica.

O loop deve funcionar como **material para exploração**.

---

# 4. Harmonic Character

O timbre deve possuir conteúdo harmônico suficiente para que o filtro produza uma transformação perceptível.

Preferir algo conceitualmente próximo de:

```text
Saw / pulse oscillator
        ↓
Subtle detune
        ↓
Low-pass filter
        ↓
Soft saturation
        ↓
Subtle modulation
        ↓
Delay / ambience
```

O objetivo não é reproduzir exatamente essa cadeia.

O objetivo é garantir que:

> abrir e fechar o filtro seja musicalmente interessante.

---

# 5. Main Interaction — Filter Knob

O elemento principal da tela é um **knob grande central**.

Ele deve ser imediatamente reconhecível como algo que pode ser girado.

O knob controla principalmente:

```text
Low-pass filter cutoff
```

Não utilizar steps ou posições discretas.

O movimento deve ser:

```text
continuous
smooth
free
responsive
```

O usuário pode girar rapidamente ou lentamente.

---

# 6. Gesture

Desktop:

```text
pointer down
    ↓
vertical drag / circular gesture
    ↓
continuous parameter change
```

Touch:

```text
touch
 ↓
drag
 ↓
continuous rotation
```

Não exigir que o usuário siga precisamente a circunferência do knob.

A interação deve ser tolerante.

Implementar:

- pointer capture;
- mouse;
- trackpad;
- touch;
- keyboard accessibility.

---

# 7. Musical Safety

A filosofia é:

> **Freedom in the hand, safety in the sound.**

O usuário pode girar livremente.

O sistema, entretanto, deve impedir regiões desagradáveis ou tecnicamente problemáticas.

Aplicar internamente:

```text
parameter clamping
parameter smoothing
gain compensation when necessary
safe resonance limits
anti-click interpolation
```

Nunca produzir mudanças abruptas de DSP.

Exemplo conceitual:

```ts
targetCutoff = mapKnobToFrequency(knobValue);

smoothedCutoff.setTargetAtTime(
  targetCutoff,
  audioContext.currentTime,
  smoothingTime,
);
```

Não mapear linearmente frequência em Hz.

Utilizar curva perceptual/logarítmica.

---

# 8. Macro Behavior

Embora visualmente exista apenas **um knob principal**, ele pode funcionar como uma macro musical.

Primary mapping:

```text
Filter cutoff
```

Secondary mappings muito sutis podem incluir:

```text
resonance
harmonic presence
saturation
delay send
stereo width
```

Esses parâmetros NÃO devem aparecer inicialmente na interface.

Exemplo:

```ts
interface BeatmakerMacroState {
  position: number; // 0 → 1
}

interface MacroMapping {
  cutoff: number;
  resonance: number;
  saturation: number;
  ambience: number;
}
```

O usuário pensa:

> “Estou abrindo o som.”

Não:

> “Estou controlando quatro parâmetros.”

---

# 9. Regra importante para Macro

Não transformar o knob em um efeito exagerado.

O comportamento deve continuar reconhecível como filtro.

Aproximadamente:

```text
80–90% perceptual effect → filter
10–20% → supporting parameters
```

Esses valores são orientação musical, não necessariamente valores literais de DSP.

---

# 10. Audio Engine

Separar completamente:

```text
UI
↓
Interaction State
↓
Parameter Mapping
↓
Audio Engine
↓
DSP
```

A UI nunca deve manipular diretamente nós internos do engine.

Criar uma API semelhante a:

```ts
interface BeatmakerEngine {
  loadSound(id: string): Promise<void>;

  play(): void;

  stop(): void;

  setMacro(value: number): void;

  setTempo(bpm: number): void;

  getPlaybackState(): PlaybackState;
}
```

---

# 11. Playback

O loop precisa ser sample-accurate ou suficientemente preciso para não apresentar drift perceptível.

Não utilizar timers comuns da UI como fonte principal de timing musical.

Evitar:

```ts
setInterval(...)
```

como clock musical.

O áudio deve possuir seu próprio scheduler/clock.

A UI apenas reflete esse estado.

---

# 12. Sequencer Visualization

A parte inferior da tela apresenta uma representação discreta dos quatro compassos.

Exemplo:

```text
| 1 · · · | 2 · · · | 3 · · · | 4 · · · |
```

Ela serve principalmente para transmitir:

- movimento;
- posição;
- repetição;
- estrutura temporal.

No MVP, ela **não precisa ser um sequenciador completo**.

Inicialmente:

```text
visualization > editing
```

O playhead deve acompanhar o áudio com precisão visual suficiente.

---

# 13. Play

Um botão Play grande inicia o loop.

```text
Stopped
   ↓
Play
   ↓
Playing
```

Quando chega ao fim do quarto compasso:

```text
Bar 4
 ↓
Bar 1
```

sem gap perceptível.

O filtro mantém sua posição durante os loops.

---

# 14. Stop

Stop deve:

- parar imediatamente;
- não resetar o knob;
- não alterar o som;
- não destruir o estado;
- retornar o playhead ao início.

Novo Play:

```text
Bar 1
```

---

# 15. Tempo

Valor inicial recomendado:

```text
90 BPM
```

Permitir posteriormente alterar BPM.

Para esta primeira implementação, manter um intervalo seguro, por exemplo:

```text
60–140 BPM
```

A UI de tempo deve ser secundária.

O BPM nunca deve competir visualmente com o knob.

---

# 16. “New Sound”

Adicionar uma ação secundária:

```text
New Sound
```

Ela permite carregar outra sequência da biblioteca.

Mas esta ação não deve ser visualmente dominante.

Fluxo:

```text
Current sound
     ↓
New Sound
     ↓
Next curated sound
```

No MVP, não abrir necessariamente um browser completo.

Pode simplesmente carregar outra opção curada.

---

# 17. Curated Sound Library

Criar uma estrutura reutilizável.

```ts
interface BeatmakerSound {
  id: string;
  name: string;

  bpm: number;
  bars: number;

  tags: string[];

  audioSource?: AudioSource;
  preset?: SynthPreset;
  sequence?: Sequence;

  macroMapping: MacroMappingConfig;
}
```

Exemplo:

```json
{
  "id": "hypnotic_analog_01",
  "name": "Slow Current",
  "bpm": 90,
  "bars": 4,
  "tags": ["analog", "hypnotic", "minimal", "chill", "warm"]
}
```

A arquitetura não deve assumir que todos os sons futuros serão sintetizadores.

Posteriormente podem existir:

```text
Synth
Sample
Texture
Bass
Drums
Found Sound
```

---

# 18. Visual Direction

Seguir a identidade visual já definida para o MusicBox:

```text
premium
professional
tactile
physical
warm
restrained
```

Materiais:

- metal escovado;
- superfícies escuras;
- iluminação âmbar;
- controles físicos;
- sombras suaves;
- pequenos LEDs;
- profundidade sutil.

Evitar aparência:

```text
gaming
neon cyberpunk
generic SaaS
flat dashboard
mobile music toy
```

---

# 19. Main Knob Visual Feedback

O knob deve possuir:

- posição física clara;
- indicador;
- iluminação sutil;
- arco mostrando o valor;
- profundidade;
- resposta durante interação.

Ao girar:

```text
pointer movement
+
knob rotation
+
arc movement
+
sound transformation
```

devem parecer uma única ação.

A resposta visual não pode chegar perceptivelmente depois da sonora.

---

# 20. Sound Visualization

Atrás ou ao redor do knob pode existir uma visualização abstrata do som.

Ela deve reagir principalmente a:

```text
filter openness
+
signal energy
```

Filtro fechado:

```text
visual smaller
darker
calmer
```

Filtro aberto:

```text
visual wider
brighter
more detailed
```

Importante:

A visualização é **feedback**, não decoração independente.

Não precisa representar literalmente uma waveform.

---

# 21. UI Hierarchy

Hierarquia visual:

```text
1. Main knob
2. Sound / motion feedback
3. Play
4. Timeline
5. New Sound
6. BPM
7. Navigation/settings
```

Se algum elemento competir com o knob, reduzir sua presença.

---

# 22. State Model

Criar estados claros.

```ts
type BeatmakerState =
  | "loading"
  | "ready"
  | "playing"
  | "stopped"
  | "changing-sound"
  | "error";
```

Separar:

```text
transport state
sound state
interaction state
```

Não criar um único componente monolítico controlando tudo.

---

# 23. Loading

Ao abrir Beatmaker:

```text
load engine
+
load first sound
```

A tela pode aparecer imediatamente.

O Play só deve ficar disponível quando o áudio estiver pronto.

Evitar spinner central.

Preferir um estado visual discreto no próprio instrumento.

---

# 24. Persistence

Persistir localmente:

```text
selected sound
macro position
tempo
```

Ao retornar ao Beatmaker, restaurar a experiência.

Não exigir:

```text
Save project
```

nesta etapa.

A sensação deve ser:

> “O instrumento lembra onde eu estava.”

---

# 25. Performance

Prioridades:

```text
audio responsiveness > animation
audio stability > visual effects
interaction latency > decorative polish
```

Objetivos:

- zero clicks;
- zero dropouts em uso normal;
- nenhuma alocação pesada durante callback crítico de áudio;
- mudanças suaves de parâmetros;
- UI responsiva durante playback.

Nunca executar análise pesada ou processamento não essencial na audio thread.

---

# 26. Accessibility

Mesmo sendo uma interface altamente visual, permitir controle via teclado.

Exemplo:

```text
Space → Play / Stop

↑ ↓
or
← →

→ adjust selected knob
```

Adicionar ARIA/semântica equivalente ao controle:

```text
Filter
0–100%
```

Não expor frequência técnica em Hz como informação principal.

---

# 27. Telemetry

Registrar apenas interação de produto.

```text
beatmaker_opened
beatmaker_played
beatmaker_macro_changed
beatmaker_sound_changed
beatmaker_tempo_changed
beatmaker_session_ended
```

Nunca registrar áudio produzido pelo usuário.

Métrica importante:

```text
Beatmaker opened
      ↓
Play
      ↓
First knob interaction
```

Medir:

```text
time_to_first_sound
time_to_first_interaction
```

---

# 28. Fora do escopo

NÃO implementar nesta etapa:

- piano roll;
- edição MIDI avançada;
- automação;
- mixer;
- tracks;
- plugins;
- routing;
- sample browser completo;
- chopping;
- arrangement;
- export;
- AI generation;
- generative music;
- advanced synth editor.

Essas funcionalidades podem surgir posteriormente.

---

# 29. Critério de sucesso

Um usuário novo deve conseguir:

```text
Open Beatmaker
      ↓
Press Play
      ↓
Hear something compelling
      ↓
See the main knob
      ↓
Turn it
      ↓
Immediately understand:
"I can shape this."
```

Tudo isso idealmente em:

```text
< 10 seconds
```

sem tutorial.

---

# 30. Regra de produto

Antes de adicionar qualquer elemento à tela, perguntar:

> **Isso torna mais divertido moldar o som ou apenas adiciona funcionalidade?**

Se for apenas funcionalidade, provavelmente não pertence à primeira experiência.

---

# North Star

O Musician começa com uma **ideia**.

O Beatmaker começa com um **som**.

Para o Beatmaker, portanto:

> **Give me something inspiring, then get out of my way.**

A primeira tela deve funcionar quase como pegar um pequeno sintetizador físico pela primeira vez: **Play → girar → ouvir → sorrir → continuar mexendo.**
