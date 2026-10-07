# MusicBox — Smart Accompaniment MVP

Implemente a primeira versão do sistema de **Smart Accompaniment** do MusicBox.

## Contexto do produto

MusicBox é um ambiente de criação musical que busca oferecer:

- simplicidade e baixa fricção;
- qualidade de áudio profissional;
- interação intuitiva e tátil;
- ferramentas e convenções familiares a músicos;
- menos complexidade operacional que uma DAW tradicional.

A persona principal é **The Musician**: guitarristas, tecladistas, baixistas, cantores, compositores e multi-instrumentistas que entendem música, mas não necessariamente querem operar uma DAW complexa.

Princípio central:

> **Play with me, not create for me.**

O MusicBox deve ajudar o músico a desenvolver uma ideia, nunca assumir controle criativo sobre ela.

---

# Objetivo

Depois que o usuário grava seu primeiro take, o MusicBox deve ser capaz de sugerir uma **base musical compatível**, começando por bateria.

Exemplo:

1. usuário grava um riff de guitarra;
2. MusicBox analisa o take;
3. estima BPM e características rítmicas;
4. consulta uma biblioteca local de grooves;
5. seleciona grooves compatíveis;
6. apresenta uma sugestão;
7. usuário pode ouvir a base junto com seu take;
8. somente após confirmação a base é adicionada ao projeto.

A sugestão deve parecer um músico acompanhando outro músico, não uma geração automática de música.

---

# Restrição importante

## Não utilizar IA generativa no MVP

Não gerar áudio, bateria, baixo ou acompanhamento através de LLMs ou modelos generativos.

O MVP deve utilizar:

**Audio analysis + metadata + musical rules + curated library.**

Isso deve garantir:

- comportamento previsível;
- baixa latência;
- funcionamento local;
- custo operacional praticamente zero;
- qualidade consistente;
- controle editorial sobre o conteúdo;
- possibilidade de uso offline.

A arquitetura, entretanto, deve permitir substituir ou complementar o motor de matching futuramente.

---

# Arquitetura conceitual

```text
Recorded Take
      ↓
Audio Analysis
      ↓
Musical Context
      ↓
Groove Matcher
      ↓
Curated Groove Library
      ↓
Ranked Suggestions
      ↓
Preview
      ↓
User Accepts
      ↓
Add to Project
```

Separar claramente:

```text
Audio Analysis
Music Intelligence
Content Library
Recommendation
Playback
UI
```

Nenhuma dessas camadas deve depender diretamente da implementação das demais.

---

# 1. Recorded Take

Quando uma gravação termina, preservar o áudio original integralmente.

O sistema de acompanhamento deve ser **não destrutivo**.

Nunca:

- alterar permanentemente o take;
- quantizar automaticamente o arquivo original;
- substituir áudio;
- aplicar processamento irreversível.

Criar uma representação de análise separada.

Exemplo conceitual:

```ts
interface RecordedTake {
  id: string;
  audioSource: AudioSource;
  duration: number;
  sampleRate: number;
  channels: number;
  createdAt: number;
}
```

---

# 2. Audio Analysis

Criar um serviço independente:

```text
AudioAnalysisService
```

Responsável inicialmente por estimar:

- BPM;
- confidence do BPM;
- onset/transient positions;
- duração musical aproximada;
- possibilidade de loop;
- beat positions;
- downbeat, se confiável.

Produzir algo semelhante a:

```ts
interface MusicalAnalysis {
  bpm: number | null;
  bpmConfidence: number;

  timeSignature?: {
    numerator: number;
    denominator: number;
    confidence: number;
  };

  beatPositions: number[];
  onsetPositions: number[];

  rhythmicDensity?: number;

  loopCandidate?: {
    start: number;
    end: number;
    confidence: number;
  };
}
```

Não inventar informações quando a confiança for baixa.

Exemplo:

```text
BPM: 96
Confidence: 0.91
Time signature: 4/4
Rhythmic density: medium
```

Se BPM confidence estiver abaixo do threshold definido, o sistema deve evitar apresentar uma sugestão como se tivesse certeza.

---

# 3. Musical Context

Converter a análise técnica em uma representação musical simples.

Exemplo:

```ts
interface MusicalContext {
  bpm?: number;
  timeSignature?: "4/4" | "3/4" | "6/8";
  rhythmicFeel?: "straight" | "swing" | "unknown";
  density?: "sparse" | "medium" | "dense";
  energy?: "soft" | "medium" | "strong";
}
```

No MVP, não é necessário inferir gênero musical.

Evitar classificações frágeis como:

```text
rock
jazz
funk
indie
```

quando o áudio não fornece evidência suficiente.

Preferir características musicais:

```text
straight
syncopated
soft
driving
sparse
dense
acoustic
electronic
```

---

# 4. Curated Groove Library

Criar uma biblioteca local de grooves profissionais.

Cada groove deve possuir áudio e metadata.

Exemplo:

```ts
interface Groove {
  id: string;
  name: string;

  sourceBpm: number;

  timeSignature: {
    numerator: number;
    denominator: number;
  };

  bars: number;

  feel: "straight" | "swing";

  density: "sparse" | "medium" | "dense";

  energy: "soft" | "medium" | "strong";

  character: string[];

  audioSource: AudioSource;

  variationGroup?: string;
}
```

Exemplo:

```json
{
  "id": "drums_042",
  "name": "Pocket 04",
  "sourceBpm": 94,
  "timeSignature": {
    "numerator": 4,
    "denominator": 4
  },
  "bars": 4,
  "feel": "straight",
  "density": "medium",
  "energy": "soft",
  "character": ["acoustic", "warm", "laid-back"]
}
```

A metadata deve ser independente dos arquivos de áudio.

---

# 5. Groove Matching Engine

Criar:

```text
GrooveMatchingService
```

Input:

```text
MusicalContext
+
GrooveLibrary
```

Output:

```ts
interface GrooveSuggestion {
  grooveId: string;
  score: number;
  reasons: string[];
}
```

O algoritmo inicial deve ser determinístico.

Exemplo de scoring:

```text
tempo compatibility       35%
time signature            25%
rhythmic feel             15%
density                    10%
energy                     10%
other characteristics      5%
```

Esses pesos devem ser configuráveis.

Não espalhar números mágicos pelo código.

---

# 6. Tempo Adaptation

Grooves devem poder ser reproduzidos no BPM detectado.

Exemplo:

```text
Original groove: 94 BPM
Recorded take:   97 BPM
Playback groove: 97 BPM
```

Utilizar time-stretching adequado para manter qualidade sonora.

Evitar pitch shifting acidental.

Definir limites aceitáveis de stretching.

Por exemplo, um groove gravado a 80 BPM não deve necessariamente ser utilizado a 145 BPM.

Adicionar metadata/configuração:

```ts
minRecommendedBpm;
maxRecommendedBpm;
```

ou calcular o range automaticamente.

---

# 7. Suggestion UX

Após terminar a gravação, não abrir modal.

Não interromper playback.

Não adicionar bateria automaticamente.

Apresentar uma sugestão discreta, por exemplo:

```text
Add a beat?
```

ou:

```text
Try a beat
```

A ação principal deve ser:

```text
Preview
```

---

# 8. Preview Mode

Preview é fundamental.

Quando acionado:

```text
Recorded Take
+
Suggested Groove
```

devem tocar sincronizados.

A base ainda NÃO pertence ao projeto.

Estado:

```ts
type AccompanimentState =
  | "idle"
  | "analyzing"
  | "suggestion-ready"
  | "previewing"
  | "accepted"
  | "dismissed";
```

Durante Preview:

- sincronizar início;
- respeitar BPM;
- manter volumes equilibrados;
- permitir parar imediatamente;
- permitir trocar a sugestão;
- não alterar o projeto.

---

# 9. Accept / Reject

Durante ou depois do preview, disponibilizar:

```text
Add
Try another
Dismiss
```

`Add`

adiciona a base ao projeto.

`Try another`

seleciona o próximo groove do ranking sem repetir imediatamente os já apresentados.

`Dismiss`

remove a sugestão da interface.

Nenhuma dessas ações deve interromper ou modificar o take original.

---

# 10. Undo

Depois de adicionar:

```text
⌘Z
```

deve remover imediatamente a base.

Adicionar acompanhamento precisa ser uma operação normal do histórico do projeto.

Não criar uma exceção especial para Smart Accompaniment.

---

# 11. Fallbacks

O sistema precisa funcionar bem quando a análise falhar.

### BPM incerto

Se:

```text
confidence < threshold
```

não fingir precisão.

Permitir que o usuário:

```text
Tap tempo
```

ou escolha BPM manualmente.

### Material sem pulso claro

Exemplos:

- pads;
- ambient guitar;
- voz;
- acordes longos;
- gravações rubato.

Nesse caso, não forçar uma bateria inadequada.

O sistema pode simplesmente não sugerir acompanhamento.

**Não sugerir nada é melhor do que sugerir algo musicalmente ruim.**

---

# 12. Performance

A análise deve acontecer localmente e preferencialmente fora da thread principal.

Objetivo perceptual:

```text
Stop recording
      ↓
short analysis
      ↓
suggestion available
```

O usuário deve conseguir reproduzir seu take imediatamente mesmo que a análise ainda esteja acontecendo.

Nunca bloquear playback aguardando análise.

---

# 13. Cache

Resultados de análise devem ser associados ao take.

```ts
interface TakeAnalysisCache {
  takeId: string;
  analysisVersion: string;
  result: MusicalAnalysis;
}
```

Não recalcular análise desnecessariamente.

Permitir invalidar cache quando o algoritmo mudar.

---

# 14. Preparação para expansão

Projetar o sistema para futuramente suportar:

```text
Drums
Percussion
Bass
Harmony
Textures
```

Portanto, evitar APIs específicas demais como:

```ts
findDrumLoop();
```

Preferir algo extensível:

```ts
findAccompaniment({
  type: "drums",
  context,
});
```

Futuramente:

```ts
findAccompaniment({
  type: "bass",
  context,
});
```

---

# 15. Harmonia — fora do MVP

Preparar arquitetura, mas NÃO implementar agora:

```text
key detection
chord detection
bass-line matching
harmonic accompaniment
```

Primeira versão:

```text
Recorded Instrument
        +
Drums
```

Depois:

```text
Recorded Instrument
        +
Drums
        +
Bass
```

E posteriormente:

```text
Instrument
+
Drums
+
Bass
+
Harmony
```

---

# 16. Telemetria local / eventos

Preparar eventos de produto sem incluir conteúdo da gravação:

```text
accompaniment_suggested
accompaniment_previewed
accompaniment_skipped
accompaniment_changed
accompaniment_accepted
accompaniment_removed
```

Nunca registrar áudio do usuário através desses eventos.

Isso permitirá medir uma métrica particularmente importante:

```text
suggestion → preview → acceptance
```

---

# 17. Critério de sucesso

O fluxo ideal deve ser:

```text
Record
   ↓
Play
   ↓
"Try a beat"
   ↓
Preview
   ↓
Add
```

O músico não deveria precisar saber:

- como BPM foi detectado;
- qual algoritmo foi utilizado;
- como o groove foi escolhido;
- como time stretching funciona;
- como sincronização está sendo realizada.

Ele simplesmente deve sentir:

> **“Eu toquei alguma coisa e o MusicBox encontrou alguém para tocar comigo.”**

---

# Princípios obrigatórios

1. Local-first.
2. Não utilizar IA generativa no MVP.
3. Áudio original sempre preservado.
4. Sugestões nunca são aplicadas automaticamente.
5. Preview antes de commit.
6. Todas as operações devem ser reversíveis.
7. Não bloquear o fluxo criativo durante análise.
8. Qualidade musical é mais importante que quantidade de sugestões.
9. Quando houver baixa confiança, assumir incerteza.
10. Não transformar a experiência em uma interface técnica de DAW.

## North Star

> **Reduce the distance between musical intention and sound.**

A implementação técnica pode ser sofisticada internamente, mas essa complexidade não deve aparecer para o músico.
