# MusicBox — Producer First Experience

Implemente a primeira experiência do modo **Producer** do MusicBox.

O objetivo é criar um ambiente extremamente rápido para **capturar e desenvolver sketches musicais**, destinado a usuários que já conhecem ferramentas profissionais de produção.

O Producer não precisa que o MusicBox substitua sua DAW.

Ele precisa que o MusicBox seja o lugar onde uma ideia começa **antes de virar uma sessão de produção**.

## Princípio central

> **Sketch first. Produce later.**

O MusicBox deve reduzir o tempo entre:

```text
Idea
 ↓
Capture
 ↓
Playback
 ↓
Experiment
 ↓
"This is something."
 ↓
Export / Continue
```

---

# 1. Persona

**The Producer** já conhece conceitos como:

- tracks;
- MIDI;
- audio recording;
- BPM;
- looping;
- quantization;
- effects;
- automation;
- stems;
- routing;
- mixing.

Não precisamos esconder esses conceitos porque são difíceis.

Precisamos evitar expô-los quando **não são necessários**.

O problema dessa persona não é falta de conhecimento.

É **overhead criativo**.

Uma DAW tradicional frequentemente transforma:

```text
"I have an idea"
```

em:

```text
Create project
Configure session
Create tracks
Choose instruments
Set routing
Find sounds
Configure monitoring
Set loop
...
```

MusicBox deve remover essa distância.

---

# 2. Primeira experiência

Ao abrir Producer:

```text
Open
 ↓
Capture or Drop
 ↓
Play
 ↓
Loop
 ↓
Add Layer / Variation
 ↓
Develop
 ↓
Export
```

Nenhum template deve ser necessário.

Nenhuma configuração inicial deve interromper esse fluxo.

---

# 3. Estado inicial

A tela começa extremamente limpa.

O elemento central é uma grande área de **Sketch**.

Antes de existir conteúdo:

```text
Record
```

e:

```text
Drop Audio / MIDI
```

devem ser as ações mais óbvias.

Não apresentar imediatamente:

- mixer;
- channel strips;
- plugin slots;
- browser complexo;
- automation lanes;
- routing;
- inspector;
- piano roll.

A mensagem implícita deve ser:

> **Put an idea here.**

---

# 4. Capture

O Producer deve conseguir começar através de:

```text
Audio recording
MIDI input
Drag & drop audio
Drag & drop MIDI
```

A arquitetura deve permitir diferentes tipos de layer:

```ts
type SketchLayerType = "audio" | "midi" | "instrument";
```

Exemplo:

```ts
interface SketchLayer {
  id: string;
  type: SketchLayerType;
  name: string;
  start: number;
  duration: number;
  muted: boolean;
  solo: boolean;
}
```

Não criar APIs dependentes exclusivamente de áudio.

---

# 5. Non-destructive by default

Toda operação deve ser não destrutiva.

Nunca modificar permanentemente:

```text
recorded audio
imported audio
MIDI source
```

Transformações devem existir como estado separado.

O usuário deve poder experimentar sem medo.

Princípio:

> **Nothing precious should be easy to destroy.**

---

# 6. Main Workspace

Depois da primeira captura, a área central apresenta o material.

Exemplo:

```text
       1       2       3       4

Guitar ████████████████████

```

Com novas layers:

```text
Guitar ████████████████████

Drums      ████████████████████

Synth              █████████████████
```

Mas evitar transformar isso imediatamente em um arranger tradicional.

O workspace é uma representação do **sketch**, não uma timeline de produção completa.

---

# 7. Transport

Disponibilizar:

```text
Record
Play
Stop
Loop
Undo
Redo
```

Esses controles devem permanecer sempre acessíveis.

Atalhos:

```text
Space → Play / Stop

R → Record

L → Loop

⌘Z / Ctrl+Z → Undo

⇧⌘Z / Ctrl+Shift+Z → Redo
```

Não substituir convenções profissionais sem uma razão forte.

---

# 8. Timing

Timing precisa transmitir confiança imediatamente.

Utilizar audio clock/scheduler adequado.

Nunca utilizar timers de UI como clock musical principal.

Evitar:

```ts
setInterval(...)
```

para scheduling crítico.

O Producer percebe rapidamente:

- drift;
- jitter;
- clicks;
- loop impreciso;
- latência inconsistente.

Esses problemas destroem a credibilidade da ferramenta.

---

# 9. BPM

BPM deve estar disponível na barra superior.

Exemplo:

```text
90 BPM
```

Permitir edição direta.

Se áudio for gravado sem BPM previamente definido, não realizar transformação destrutiva automática.

Se houver detecção automática:

```ts
interface TempoAnalysis {
  bpm: number | null;
  confidence: number;
}
```

Nunca apresentar baixa confiança como certeza.

---

# 10. Loop

Loop é central para essa persona.

O usuário deve conseguir transformar rapidamente uma captura em material repetível.

Fluxo:

```text
Capture
 ↓
Select region
 ↓
Loop
 ↓
Continue playing over it
```

Loop precisa ser:

- gapless;
- previsível;
- reversível.

---

# 11. Add Layer

Depois que existe um sketch, disponibilizar:

```text
Add Layer
```

Essa ação permite:

```text
Record audio
Record MIDI
Drop audio
Drop MIDI
Add MusicBox instrument
```

Não abrir inicialmente uma janela enorme.

Preferir escolha contextual pequena.

---

# 12. Progressive Complexity

Esse princípio é obrigatório.

Começar com:

```text
1 idea
```

Depois:

```text
+ Layer
```

Depois:

```text
+ Variation
```

Depois:

```text
+ Develop
```

A complexidade deve aparecer como consequência da criação.

Nunca apresentar toda a capacidade do MusicBox antes que seja necessária.

---

# 13. Create Variation

Uma das ações centrais é:

```text
Create Variation
```

Ela deve criar uma ramificação segura do sketch atual.

Conceitualmente:

```text
Sketch A
   │
   ├── Variation B
   │
   └── Variation C
```

Não implementar inicialmente como version control complexo.

Para o usuário:

```text
Create Variation
```

significa:

> “Quero experimentar outra direção sem perder esta.”

---

# 14. Variation Data Model

Preparar uma arquitetura simples:

```ts
interface Sketch {
  id: string;
  parentId?: string;

  layers: SketchLayer[];

  bpm: number;

  createdAt: number;
}
```

Criar variation pode inicialmente duplicar estado estrutural utilizando referências imutáveis quando apropriado.

Evitar duplicação desnecessária de arquivos grandes de áudio.

---

# 15. Develop

A ação:

```text
Develop
```

marca a transição entre:

```text
Sketching
```

e:

```text
Deeper creation
```

Ela pode futuramente revelar:

- drums;
- bass;
- instruments;
- effects;
- editing;
- arrangement.

No MVP, Develop pode simplesmente abrir o próximo nível do workspace.

Importante:

> **Develop não deve existir como um botão mágico que gera uma música.**

É uma porta para ferramentas adicionais.

---

# 16. Export

Export deve permanecer sempre visível.

Isso é particularmente importante para The Producer.

A interface precisa comunicar:

> **Your idea is not trapped here.**

Primeira versão:

```text
Export
 ├── Mix WAV
 ├── Stems
 └── MIDI
```

MIDI deve aparecer apenas quando aplicável.

---

# 17. Mix Export

Permitir:

```text
WAV
```

com configuração padrão profissional sensata.

Não exigir uma janela cheia de parâmetros para export rápido.

Fluxo ideal:

```text
Export
 ↓
Mix WAV
 ↓
Export
```

Configurações avançadas podem existir posteriormente.

---

# 18. Stems

Exportar cada layer separadamente.

Exemplo:

```text
01_Guitar.wav
02_Drums.wav
03_Synth.wav
```

Todos os stems devem:

- começar no mesmo ponto temporal;
- possuir duração coerente;
- manter sincronização;
- importar corretamente em outra DAW.

Essa funcionalidade é crítica.

---

# 19. MIDI Export

Layers MIDI devem poder ser exportadas como MIDI padrão.

Preservar quando possível:

```text
notes
velocity
timing
duration
tempo context
```

Não rasterizar MIDI desnecessariamente para áudio.

---

# 20. DAW Handoff

Projetar Export pensando explicitamente no fluxo:

```text
MusicBox
 ↓
Sketch
 ↓
Export
 ↓
Ableton / Logic / Pro Tools / Reaper / etc.
```

Não implementar integrações específicas com DAWs no MVP.

Arquitetura deve permitir isso futuramente.

---

# 21. Quick Actions

Na parte inferior do workspace, apresentar:

```text
Add Layer
Create Variation
Develop
```

Essas ações devem ser contextuais.

Antes de existir um sketch, elas podem estar ausentes ou visualmente reduzidas.

Depois da primeira captura:

```text
Quick Actions
```

aparece naturalmente.

---

# 22. Visual Hierarchy

Prioridade visual:

```text
1. Captured idea
2. Transport
3. Add Layer / Variation / Develop
4. Export
5. BPM / Metronome
6. Secondary navigation
```

O protagonista é sempre:

> **the idea**

Não os controles.

---

# 23. Visual Identity

Manter a identidade definida para MusicBox:

- walnut / madeira escura;
- brushed metal;
- superfícies escuras;
- knobs físicos;
- iluminação âmbar;
- LEDs discretos;
- profundidade;
- materiais táteis;
- acabamento premium.

Entretanto, o Producer deve utilizar **menos skeuomorphism que o Musician**.

A tela deve parecer:

```text
professional
calm
precise
trustworthy
```

e não:

```text
complex
technical
busy
DAW-like
```

---

# 24. Track Controls

Quando houver múltiplas layers, permitir controles mínimos:

```text
Mute
Solo
Level
```

Evitar inicialmente:

```text
Pan
Send
Insert
Bus
Input routing
Output routing
```

Esses conceitos podem surgir posteriormente através de Develop.

---

# 25. Undo / Redo

Undo deve ser extremamente confiável.

Todas as operações estruturais devem entrar no histórico:

```text
record
delete
move
add layer
remove layer
create variation
change tempo
loop changes
```

A confiança do Producer depende fortemente disso.

---

# 26. Autosave

Salvar continuamente em background.

Nunca exigir:

```text
Save Project
```

durante o fluxo inicial.

Criar conceito de:

```text
Sketch
```

em vez de exigir que o usuário crie um projeto formal.

---

# 27. Recovery

Se o app fechar inesperadamente:

```text
reopen
 ↓
restore last sketch
```

com o mínimo possível de perda.

Capturas de áudio devem ser persistidas de forma segura imediatamente após gravação.

---

# 28. State Model

Separar claramente:

```ts
interface ProducerState {
  transport: TransportState;
  sketch: SketchState;
  selection: SelectionState;
  export: ExportState;
}
```

Não criar um componente gigante responsável por:

```text
audio
timeline
transport
export
UI
```

Separar responsabilidades.

---

# 29. Architecture

Estrutura conceitual:

```text
Producer UI
      ↓
Sketch Controller
      ↓
Command / History Layer
      ↓
Audio + MIDI Engine
      ↓
Persistence
```

Export deve funcionar como serviço independente:

```text
ExportService
```

e não depender diretamente de componentes da interface.

---

# 30. Performance

Prioridade:

```text
Audio stability
>
Input latency
>
Transport precision
>
UI animation
```

O workspace pode simplificar animações quando necessário.

Nunca comprometer áudio para manter efeitos visuais.

---

# 31. Trust Indicators

Feedback deve ser discreto e preciso.

Exemplos:

```text
Recording
Saved
Loop active
Export complete
```

Evitar notificações excessivas.

Não usar mensagens como:

```text
Awesome!
Amazing take!
🔥
```

O Producer não precisa que o software avalie sua criação.

---

# 32. Errors

Erros devem ser humanos, mas precisos.

Evitar:

```text
Something went wrong.
```

Preferir:

```text
Your audio input became unavailable.
Recording stopped safely.
```

Sempre preservar o que já foi capturado quando possível.

---

# 33. Telemetry

Registrar apenas eventos estruturais.

```text
producer_opened

producer_record_started

producer_first_capture

producer_layer_added

producer_variation_created

producer_develop_opened

producer_export_started

producer_export_completed
```

Nunca enviar conteúdo de áudio ou MIDI sem consentimento explícito.

---

# 34. Métricas

Métricas principais:

```text
time_to_first_capture
```

e:

```text
time_to_first_playback
```

Depois:

```text
capture
 ↓
second layer
```

e:

```text
sketch
 ↓
export
```

Esses eventos ajudam a descobrir se MusicBox realmente reduz overhead.

---

# 35. Fora do MVP

Não implementar inicialmente:

- mixer completo;
- automation lanes;
- plugin hosting;
- advanced routing;
- buses;
- mastering;
- comping complexo;
- advanced MIDI editing;
- full piano roll;
- advanced waveform editor;
- score editor;
- DAW-specific integrations.

O objetivo não é competir feature-by-feature com uma DAW.

---

# 36. First Minute Test

Um Producer experiente deve conseguir abrir o MusicBox sem tutorial e fazer:

```text
Open
 ↓
Record
 ↓
Play
 ↓
Loop
 ↓
Add Layer
```

em menos de um minuto.

E entender imediatamente como sair com o material através de:

```text
Export
```

---

# 37. Critério emocional de sucesso

Depois de aproximadamente um minuto, a reação desejada não é:

> “Nossa, isso tem muitos recursos.”

É:

> **“Isso é rápido.”**

E alguns minutos depois:

> **“Tenho alguma coisa aqui. Vou continuar.”**

---

# Regra de produto

Sempre que surgir a proposta de adicionar uma funcionalidade ao Producer, perguntar:

> **Isso ajuda a capturar ou desenvolver uma ideia antes de entrar na DAW?**

Se a resposta for não, provavelmente não pertence ao Producer MVP.

---

# North Star

**The Musician:**

> “Play with me.”

**The Beatmaker:**

> “Give me something inspiring to shape.”

**The Producer:**

> **“Get the idea down before it disappears.”**

MusicBox não deve substituir o ambiente profissional do Producer.

Deve conquistar um espaço diferente:

> **The place where the track begins.**
