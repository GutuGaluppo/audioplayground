# ADR-011: Smart Accompaniment MVP

**Status**: Accepted (2026-10-07). As decisões de implementação do MVP estão na última seção.  
**Relates to**: [ADR-010: Effect Buses](ADR-010-effect-buses.md)

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
  "character": [
    "acoustic",
    "warm",
    "laid-back"
  ]
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
minRecommendedBpm
maxRecommendedBpm
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

# 11. Fallbacks e tratamento de incerteza

O sistema precisa funcionar bem quando a análise for imprecisa ou impossível.

### BPM incerto

Se:

```text
confidence < threshold (padrão: 0.70)
```

não fingir precisão.

Permitir que o usuário:

```text
Tap tempo
```

ou escolha BPM manualmente.

Sugestão não deve ser apresentada até que um BPM confiável exista (automaticamente detectado ou manualmente informado).

### Material sem pulso claro

**Não sugerir acompanhamento quando:**

- pads, ambient guitar, texturas longas (sem onset claro);
- voz acapela, vocalizes, speech;
- acordes longos sem padrão rítmico;
- gravações com andamento rubato inconsistente;
- densidade de onset < threshold mínimo (ex: menos de 0.5 onsets por segundo).

**Critério**: Se não há pelo menos 50% de confiança em um BPM e uma estrutura rítmica mínima, não sugerir.

O sistema deve exibir uma mensagem amigável:

```text
"Couldn't find a rhythm pattern here. Try tapping the tempo or add drums manually."
```

**Princípio**: Não sugerir nada é melhor do que sugerir algo musicalmente ruim.

### Falha de análise (erro técnico)

Se a análise falhar (arquivo corrompido, falta de memória, timeout):

- registrar erro em log;
- exibir mensagem ao usuário (ex: "Analysis failed, try again later");
- não bloquear playback do take;
- permitir usuário adicionar bateria manualmente.

Não falhar silenciosamente.

---

# 12. Performance

A análise deve acontecer localmente e preferencialmente fora da thread principal.

Objetivo perceptual:

```text
Stop recording
      ↓
short analysis (< 1s)
      ↓
suggestion available
```

O usuário deve conseguir reproduzir seu take imediatamente mesmo que a análise ainda esteja acontecendo.

Nunca bloquear playback aguardando análise.

### Latência aceitável

- Análise de BPM: < 500ms para gravação de até 2 minutos
- Matching: < 200ms
- Preview start: < 100ms (renderização de áudio time-stretched)

Testar em macOS (Apple Silicon + Intel) e plataformas desktop alvo.

Profilear e otimizar se latência exceder os limites acima.

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
findDrumLoop()
```

Preferir algo extensível:

```ts
findAccompaniment({
  type: "drums",
  context
})
```

Futuramente:

```ts
findAccompaniment({
  type: "bass",
  context
})
```

---

# 15. Harmonic Analysis — fora do MVP

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

---

# Decisões de implementação (MVP)

O texto acima é a especificação de produto. Estas são as escolhas feitas ao implementá-la, e onde o MVP se afasta dela de propósito.

## O que foi construído

```text
AudioAnalysis    core/analysis/RhythmAnalysis        onsets, BPM + confiança, beats, feel
Music context    core/accompaniment/MusicalContext   análise -> contexto + veredito
Library          presets/grooves.json (embutido)     12 grooves e seus metadados
Recommendation   core/accompaniment/Grooves          findAccompaniment(kind, contexto)
Service/preview  apps/desktop/AccompanimentService   estados, preview, add, eventos
UI               components/AccompanimentBar          faixa discreta + "Try a beat" na timeline
```

As camadas do núcleo não dependem da UI nem do desktop. `findAccompaniment(Kind::drums, ...)` já é a API extensível do §14; só `drums` existe.

## Onde o MVP difere da especificação

1. **A biblioteca são padrões do kit de bateria interno, não áudio gravado.** Um groove é um compasso de 16 passos tocado pela bateria do app, com os metadados do §4. Vantagens: nenhuma licença de amostras, nada a esticar (**não há time-stretching**: o padrão simplesmente segue o BPM do projeto), preview imediato e a base entra no projeto como um clipe de padrão comum, editável. Grooves de áudio curados continuam possíveis depois como outro `Kind` ou fonte, sem mudar o matcher.
2. **O andamento do projeto é fixo (decisão D4), então a base toca no BPM do projeto, não no detectado.** A análise serve para **validar**: o take precisa estar no mesmo andamento (±4 %, ou o dobro/metade) e com os beats sobre a grade do projeto (±15 % de um tempo). Caso contrário não há sugestão, e a mensagem diz o que fazer ("parece ~87 BPM, mas o projeto está em 120", "o take não está no tempo do metrônomo"). Mudar o BPM do projeto para o do take fica para depois (mexe em todos os clipes). Tap tempo: o campo de BPM que já existe cumpre esse papel.
3. **Compasso:** a biblioteca só tem 4/4; um projeto em outro compasso não recebe sugestão. O compasso do take não é detectado (o projeto manda).
4. **Sem `loopCandidate`** na análise (não é usado pelo MVP).

## Como funciona

- **Análise** (`analyseRhythm`): fluxo espectral em magnitudes logarítmicas (FFT 1024, passo de 10 ms), picos com limiar local e absoluto, andamento por autocorrelação com preferência suave por 110 BPM (faixa 50–220), confiança = quanto da curva se repete, fase dos beats, feel (straight/swing) pela posição dos ataques fora do tempo. Puro e determinístico. 62 ms para 2 minutos de áudio em release (meta: < 500 ms). Roda numa thread de trabalho e nunca atrasa o playback.
- **Veredito** (`makeContext`): `noRhythm` (< 0,5 ataque/s ou confiança < 0,5), `tempoUncertain` (0,5–0,7), `tempoMismatch`, `offGrid`, ou `ready`. Densidade, energia e feel viram palavras; os limiares estão em `ContextSettings`.
- **Matching** (`findAccompaniment`): pesos do §5 em `MatchWeights` (configuráveis), score normalizado, empates por id, nunca um groove fora do compasso ou do seu intervalo de BPM, nada abaixo de `minimumScore`. "Try another" exclui os já mostrados.
- **Preview:** o serviço monta uma **cópia hipotética do projeto** com a base e a publica no motor (`Session::setPreview`); o projeto aberto e o histórico não mudam. O preview termina com qualquer edição real, ao parar o transporte, ao abrir outro projeto, ao adicionar ou dispensar.
- **Add:** um passo de undo ("Add drums") na trilha de bateria (criada se faltar), do compasso onde o take começa até o fim dele. ⌘Z remove e a oferta volta. Se já há bateria nesse trecho, não se sugere.
- **Cache:** resultado por asset e versão do algoritmo, em memória.
- **Eventos locais (§16):** `accompaniment_suggested/previewed/skipped/changed/accepted/removed` chegam a um callback; só nomes, sem áudio nem arquivos, e nada é enviado a lugar nenhum.
- **Gatilhos:** ao terminar uma gravação de áudio (automático) e o botão "Try a beat" com um clipe de áudio selecionado.

## Limites conhecidos

- **Validado com sinais sintéticos e com áudio renderizado pelo próprio motor** (baterias, riffs de synth, acordes sustentados, ruído, jitter, swing). **Ainda não com gravações reais** de violão, guitarra, teclado ou voz. Os limiares (em especial `minPeakFlux`) foram calibrados nesses sinais e provavelmente precisam de ajuste com material real. Isso é uma verificação humana pendente.
- O andamento pode sair em oitava (um pulso rápido lido como metade); o veredito aceita dobro/metade em relação ao projeto.
- Material que não é percussivo nem articulado (pads, voz, acordes longos) cai em `noRhythm`, como pede o §11; pode haver falsos negativos em takes muito suaves.

