# Creative Audio Playground — Development Guide

**Status:** Initial architecture / MVP definition  
**Audience:** Claude Code, Codex, human contributors  
**Product type:** Desktop-first creative audio playground, designed to expand to web and mobile  
**Primary goal:** Let users create, record, manipulate, arrange, and export sound with minimal friction.

---

## 1. Product Principle

This product is **not a traditional DAW**.

The experience should feel closer to a **digital musical instrument / audio playground**:

> Open → Play → Record → Shape → Combine → Export

A user should be able to create something meaningful without understanding buses, routing matrices, plugin formats, gain staging, or advanced music theory.

### Product values

1. **Immediate sound** — interaction should produce audible feedback quickly.
2. **Low cognitive load** — expose musical controls before technical controls.
3. **Playfulness without toy-like limitations** — simple interface, serious audio quality.
4. **Non-destructive editing** — preserve original recordings whenever possible.
5. **Local-first creation** — core creation must not depend on cloud services.
6. **Own the DSP** — prefer first-party effects and instruments when practical.
7. **Portable audio engine** — UI and audio processing must remain independent.

---

# 2. MVP Scope

The first usable version must allow a user to create a short musical idea from scratch and export it.

## 2.1 Core workflow

```text
Create Project
    ↓
Choose sound source
    ├─ Drum Machine
    ├─ Synth
    ├─ Sampler
    ├─ Microphone / Audio Input
    └─ Imported Audio
    ↓
Play / Record
    ↓
Loop / Arrange
    ↓
Apply Effects
    ↓
Mix Basic Levels
    ↓
Save
    ↓
Export WAV
```

## 2.2 MVP features

### Play
- Pad grid
- Basic keyboard input
- Drum machine
- Simple subtractive synth
- Sample playback
- Tempo control
- Metronome

### Record
- Microphone / audio input recording
- Basic input level meter
- Record into a track/layer
- Overdub
- Count-in

### Arrange
- Timeline
- Audio clips
- MIDI/note clips
- Move clips
- Trim clips
- Split clips
- Duplicate clips
- Loop clips
- Snap to grid
- Undo / redo

### Shape
- Gain
- Pan
- 3-band parametric EQ
- Compressor
- Low-pass / high-pass filter
- Delay
- Reverb
- Distortion / overdrive

### Mix
- Track volume
- Track mute
- Track solo
- Master output level
- Peak meter

### Project
- New project
- Save
- Load
- Autosave
- Project metadata
- Local asset management

### Export
- WAV export
- Stereo master
- Configurable sample rate

---

# 3. Explicit Non-Goals for MVP

Do **not** implement these unless explicitly requested:

- VST3 hosting
- Audio Unit hosting
- AAX
- Cloud collaboration
- Marketplace
- AI music generation
- Stem separation
- Mastering assistant
- Advanced automation lanes
- Surround audio
- Video timeline
- Score notation
- Advanced MIDI editing
- Modular synthesis environment
- Unlimited routing graph
- Full professional mixer
- Plugin sandboxing
- Social network
- Subscription/payment system

Avoid adding features just because mature DAWs contain them.

---

# 4. Recommended Architecture

## 4.1 Stack

| Layer | Technology |
|---|---|
| Audio engine | C++ |
| Audio framework | JUCE |
| DSP | C++ / JUCE DSP + custom algorithms |
| Build system | CMake |
| Desktop shell | JUCE native application |
| Web audio target | WebAssembly + Web Audio API + AudioWorklet |
| Web UI | React + TypeScript |
| UI state | Zustand |
| Local metadata | SQLite |
| Browser storage | IndexedDB |
| Graphics | Native GPU rendering / Canvas / WebGL as appropriate |
| Tests | Catch2 or GoogleTest for C++; Vitest for TypeScript |
| Monorepo | pnpm workspaces + CMake projects |

Do not couple DSP implementation to React, browser APIs, or visual controls.

---

# 5. Architectural Rule: DSP Is Headless

Every audio processor must work without a UI.

Bad:

```text
CompressorKnob → CompressorAudioCode
```

Good:

```text
UI Knob
   ↓
Parameter Model
   ↓
CompressorProcessor
   ↓
Audio Buffer
```

A processor should expose parameters through a stable interface.

Example:

```cpp
struct CompressorParams {
    float thresholdDb;
    float ratio;
    float attackMs;
    float releaseMs;
    float makeupGainDb;
};

class CompressorProcessor {
public:
    void prepare(double sampleRate, int maxBlockSize);
    void setParameters(const CompressorParams& params);
    void process(AudioBufferView buffer);
    void reset();
};
```

No visual or platform-specific code belongs in the processor.

---

# 6. Proposed Repository Structure

```text
creative-audio-playground/
│
├── apps/
│   ├── desktop/
│   └── web/
│
├── audio/
│   ├── engine/
│   │   ├── transport/
│   │   ├── graph/
│   │   ├── tracks/
│   │   ├── mixer/
│   │   └── rendering/
│   │
│   ├── dsp/
│   │   ├── filters/
│   │   ├── eq/
│   │   ├── dynamics/
│   │   ├── delay/
│   │   ├── reverb/
│   │   ├── distortion/
│   │   └── utilities/
│   │
│   ├── instruments/
│   │   ├── synth/
│   │   ├── drum-machine/
│   │   └── sampler/
│   │
│   └── io/
│       ├── recording/
│       ├── devices/
│       ├── decoding/
│       └── encoding/
│
├── packages/
│   ├── project-model/
│   ├── shared-types/
│   ├── presets/
│   └── ui-components/
│
├── tests/
│   ├── audio/
│   ├── integration/
│   └── fixtures/
│
├── docs/
│   ├── architecture/
│   ├── dsp/
│   └── decisions/
│
├── CMakeLists.txt
├── pnpm-workspace.yaml
├── AGENTS.md
└── README.md
```

Keep repository boundaries clear even if the first implementation contains only a subset of these directories.

---

# 7. Audio Engine Responsibilities

The audio engine owns:

- Audio device lifecycle
- Sample rate
- Block size
- Audio callback
- Transport
- Playback position
- Recording
- Track processing
- Instrument rendering
- Effect processing
- Mixing
- Master output
- Offline rendering/export

The UI must never perform audio processing.

---

# 8. Real-Time Audio Rules

The real-time audio callback is a hard boundary.

Inside the audio thread:

### Allowed
- Preallocated buffer processing
- Lock-free reads
- Atomic parameter reads
- DSP math
- Fixed-size data structures

### Forbidden
- Heap allocation
- File I/O
- Network requests
- Logging to console
- Mutex waits
- UI updates
- Database calls
- JSON parsing
- Dynamic container growth

If uncertain whether an operation is real-time safe, treat it as unsafe until verified.

---

# 9. Audio Graph — MVP

Keep routing intentionally simple.

```text
Source
  ↓
Track Gain
  ↓
EQ
  ↓
Compressor
  ↓
Insert FX 1
  ↓
Insert FX 2
  ↓
Pan
  ↓
Track Volume
  ↓
Master
  ↓
Limiter
  ↓
Audio Output
```

Do not build a generic modular graph editor during MVP.

Internally, the architecture may use a graph abstraction if it simplifies future routing.

---

# 10. Track Model

Initial track types:

```text
AudioTrack
InstrumentTrack
```

Suggested common interface:

```cpp
class Track {
public:
    TrackId id;
    std::string name;

    float volume;
    float pan;
    bool muted;
    bool solo;

    EffectChain effects;
};
```

Audio tracks contain audio clips.

Instrument tracks contain note clips and an instrument.

Avoid premature inheritance hierarchies. Prefer composition where possible.

---

# 11. Project Data Model

Project data and binary audio assets must be separated.

Example:

```text
My Song/
├── project.json
├── audio/
│   ├── recording-001.wav
│   └── sample-001.wav
├── cache/
└── autosave/
```

Suggested project model:

```json
{
  "version": 1,
  "name": "Untitled",
  "sampleRate": 48000,
  "tempo": 120,
  "timeSignature": [4, 4],
  "tracks": [],
  "createdAt": "...",
  "updatedAt": "..."
}
```

Project files require explicit schema versioning from day one.

Never silently destroy unsupported project data during migration.

---

# 12. DSP Modules — MVP

## 12.1 Filter

Implement first.

Required modes:

- Low-pass
- High-pass

Parameters:

```text
frequency
resonance/Q
mode
```

---

## 12.2 Parametric EQ

Three bands:

```text
Low
Mid
High
```

Each band:

```text
frequency
Q
gain
```

The architecture should allow additional bands later.

---

## 12.3 Compressor

Parameters:

```text
threshold
ratio
attack
release
makeup gain
```

Optional later:

```text
knee
sidechain
lookahead
mix
```

---

## 12.4 Delay

Parameters:

```text
time
feedback
mix
```

Add tempo sync after the basic version is stable.

---

## 12.5 Reverb

MVP may begin with a known lightweight algorithm.

Parameters:

```text
size
decay
damping
mix
```

Convolution reverb is not required initially.

---

## 12.6 Distortion

Start with waveshaping.

Parameters:

```text
drive
tone
output
mix
```

Future modules can include:

- Overdrive
- Fuzz
- Saturation
- Bitcrusher

---

# 13. Instruments

## 13.1 Synth V1

Use subtractive synthesis.

Architecture:

```text
Oscillator
    ↓
Filter
    ↓
Envelope
    ↓
Amplifier
```

Minimum controls:

```text
waveform
pitch
detune
filter cutoff
filter resonance
attack
decay
sustain
release
volume
```

Waveforms:

- Sine
- Triangle
- Saw
- Square

Polyphony target for MVP: **8–16 voices**.

---

## 13.2 Drum Machine

Initial design:

```text
4 × 4 pads
```

Each pad can:

- Load sample
- Trigger sample
- Adjust volume
- Adjust pitch
- Mute

Sequencer:

```text
16 steps
```

Start with one bar at 4/4.

---

## 13.3 Sampler

MVP:

- Load WAV
- Trigger sample
- Start/end position
- Pitch
- Gain
- One-shot / gate mode

Advanced slicing is postponed.

---

# 14. Timeline

The timeline should remain simpler than a traditional DAW.

Essential actions:

```text
select
move
trim
split
duplicate
delete
loop
```

Initial ruler:

```text
Bars / Beats
```

Optional later:

```text
Seconds
Timecode
```

Clip edits must be non-destructive whenever possible.

---

# 15. Undo / Redo Architecture

Use explicit commands.

Example:

```text
MoveClipCommand
SplitClipCommand
DeleteClipCommand
ChangeParameterCommand
AddTrackCommand
```

Every destructive user action must be undoable unless technically impossible.

Do not implement undo by serializing the entire project after every action.

---

# 16. Parameter System

Effects and instruments must share a common parameter concept.

Each parameter should define:

```text
id
name
min
max
default
normalized value
unit
skew/curve
```

Example IDs:

```text
compressor.threshold
compressor.ratio
filter.cutoff
reverb.mix
synth.attack
```

Parameter IDs are persistence contracts.

Do not rename them casually after release.

---

# 17. Presets

Presets should be plain structured data rather than opaque blobs.

Example:

```json
{
  "id": "delay.space-echo-01",
  "processor": "delay",
  "name": "Space Echo",
  "version": 1,
  "parameters": {
    "time": 0.38,
    "feedback": 0.62,
    "mix": 0.35
  }
}
```

Built-in presets should live in source-controlled files.

---

# 18. Desktop First

The first production target should be desktop.

Recommended priority:

```text
1. macOS
2. Windows
3. Web
4. iOS
5. Android
```

This is a development-order recommendation, not a permanent product restriction.

Desktop provides the most predictable environment for:

- Low-latency audio
- Audio interfaces
- MIDI devices
- Local files
- Longer sessions
- DSP profiling

---

# 19. Web Strategy

The web version should reuse DSP through WebAssembly where useful.

Suggested architecture:

```text
React UI
   ↓
TypeScript Audio Controller
   ↓
AudioWorklet
   ↓
WASM DSP Core
   ↓
Web Audio Graph
```

Do not call heavy DSP directly from React rendering code.

React owns interface state.

The audio layer owns audio state.

Synchronize only the information the UI needs.

---

# 20. State Separation

Use three conceptual state domains.

## UI State

Examples:

```text
selected panel
open modal
zoom
selection
hover state
```

## Project State

Examples:

```text
tracks
clips
tempo
processor parameters
project name
```

## Real-Time Audio State

Examples:

```text
playhead
meter values
active voices
sample position
```

Never put high-frequency audio state directly into React global state.

Meters and playhead should use throttled snapshots.

---

# 21. Performance Targets

Treat these as engineering targets, not guaranteed marketing claims.

### Audio

- No audible glitches under normal project load
- Stable 44.1 kHz and 48 kHz operation
- Support common buffer sizes such as 64 / 128 / 256 / 512 samples
- Avoid allocations inside audio callback

### UI

- Target 60 FPS interactions
- Waveform rendering must not block audio
- Timeline scrolling must remain responsive

### Startup

- Avoid scanning unnecessary resources at launch
- Load project metadata before heavy assets where practical

---

# 22. Testing Strategy

Audio code requires more than UI tests.

## Unit tests

Test:

- Parameter mapping
- Filters
- Envelope behavior
- Compressor gain reduction
- Delay timing
- Transport calculations
- Clip position math
- Project serialization

## DSP golden tests

Use deterministic audio fixtures.

```text
input.wav
   ↓ DSP
output.wav
   ↓
Compare against reference
```

Allow an appropriate numerical tolerance.

## Integration tests

Examples:

```text
Create project
Add track
Add synth
Create notes
Render
Verify output exists and is non-silent
```

## Manual audio QA

Automated tests cannot detect every audible artifact.

Maintain a short listening checklist for releases.

---

# 23. Logging

Logging must happen outside the real-time audio callback.

Useful categories:

```text
app
project
audio-device
audio-engine
midi
export
persistence
```

Never flood logs with per-buffer events.

---

# 24. Error Handling

Audio applications must fail gracefully.

Examples:

### Audio device unavailable

Show:

```text
Audio device unavailable.
Choose another output device.
```

Do not crash.

### Missing audio asset

Keep the clip metadata and show it as missing.

Do not silently delete it from the project.

### Failed project migration

Preserve the original project.

Never overwrite it with partially migrated data.

---

# 25. Security / Privacy

Core creation is local-first.

Rules:

- Do not upload recordings without explicit user action.
- Do not enable microphone access before it is required.
- Do not send audio telemetry.
- Do not store raw microphone content in logs.
- Keep analytics separate from creative content.
- Treat imported and recorded material as private user data.

---

# 26. Development Phases

## Phase 0 — Foundation

Goal: prove stable audio output.

Deliverables:

- Repository
- CMake
- JUCE integration
- Audio device initialization
- Tone generator
- Basic transport
- Unit-test setup

Acceptance:

> App launches and plays a clean generated tone without glitches.

---

## Phase 1 — Playground Core

Deliverables:

- Pad grid
- Drum sampler
- 16-step sequencer
- Tempo
- Start/stop
- Simple synth

Acceptance:

> User can build and play a basic beat plus melodic sound.

---

## Phase 2 — Recording

Deliverables:

- Input device selection
- Input meter
- Record audio
- Playback recording
- Basic waveform

Acceptance:

> User can record a microphone/instrument and immediately replay it.

---

## Phase 3 — Arrangement

Deliverables:

- Timeline
- Tracks
- Clips
- Move
- Trim
- Split
- Loop
- Duplicate
- Snap

Acceptance:

> User can create a short structured composition from multiple clips.

---

## Phase 4 — First-Party FX

Deliverables:

- Filter
- EQ
- Compressor
- Distortion
- Delay
- Reverb
- Effect chain
- Presets

Acceptance:

> User can meaningfully reshape recorded and generated sounds.

---

## Phase 5 — Save / Export

Deliverables:

- Project format v1
- Save
- Open
- Autosave
- WAV export
- Offline rendering

Acceptance:

> User can close the app, reopen the project, and export the same composition.

---

## Phase 6 — Product Polish

Deliverables:

- Performance profiling
- Device handling
- Crash recovery
- Better presets
- Onboarding
- Accessibility
- UX refinement

Acceptance:

> Product is usable by someone unfamiliar with DAWs without documentation.

---

# 27. MVP Definition of Done

The MVP is complete when a new user can:

1. Open the app.
2. Start a new project.
3. Create a beat.
4. Play a synth sound.
5. Record audio.
6. Arrange several clips.
7. Apply at least EQ, compressor, delay, reverb, and distortion.
8. Change track levels.
9. Save the project.
10. Reopen it successfully.
11. Export a stereo WAV.

Anything not necessary for this journey is secondary.

---

# 28. Coding Standards

## C++

- Prefer RAII.
- Avoid owning raw pointers.
- Use explicit ownership semantics.
- Prefer small DSP classes.
- Avoid unnecessary inheritance.
- Keep platform-specific code isolated.
- Use `std::unique_ptr` by default for exclusive dynamic ownership.
- Use atomics / lock-free structures intentionally, not reflexively.
- Document real-time safety assumptions.

## TypeScript

- Strict mode required.
- Avoid `any` unless integration boundaries make it unavoidable.
- Prefer explicit domain types.
- Keep React components focused on presentation and interaction.
- Keep audio orchestration outside React components.
- Avoid duplicating DSP parameter definitions in multiple layers.

---

# 29. Dependency Policy

Before adding a dependency, answer:

1. What problem does it solve?
2. Can the standard library / JUCE / existing dependency already solve it?
3. Is it actively maintained?
4. Is its license compatible with commercial distribution?
5. Does it add runtime or binary weight?
6. Is it safe for real-time audio use where relevant?

Avoid dependency accumulation.

For DSP that defines the product's identity, prefer implementing and owning the algorithm when practical.

---

# 30. Licensing Rule

Every third-party audio algorithm, library, sample pack, impulse response, preset source, and bundled asset must have a recorded license.

Create:

```text
docs/THIRD_PARTY_LICENSES.md
```

Do not assume that "free" means commercially redistributable.

---

# 31. Architecture Decision Records

Important irreversible or expensive decisions should receive an ADR.

Store in:

```text
docs/decisions/
```

Format:

```text
ADR-001-short-title.md

Context
Decision
Alternatives
Consequences
```

Examples:

```text
ADR-001-juce-audio-core.md
ADR-002-project-file-format.md
ADR-003-dsp-wasm-strategy.md
```

---

# 32. Instructions for Claude Code and Codex

This section is an execution contract for coding agents.

## Before modifying code

1. Read this document.
2. Read `AGENTS.md` if present.
3. Inspect the existing implementation before proposing new architecture.
4. Identify the smallest change required.
5. Preserve existing behavior unless the task explicitly changes it.
6. Check tests related to the affected subsystem.

## While implementing

- Prefer small, reviewable changes.
- Do not perform unrelated refactors.
- Do not replace working architecture solely for stylistic preference.
- Avoid adding dependencies without justification.
- Keep DSP code independent of UI code.
- Preserve real-time safety.
- Add or update tests alongside behavior changes.
- Use existing naming patterns.
- Do not duplicate parameter definitions.
- Do not silently change persisted schemas.

## Before finishing

Run the relevant checks.

Expected categories:

```text
format
lint
unit tests
audio/DSP tests
integration tests
build
```

If a command cannot be run, state exactly why.

Do not claim success without running the relevant validation where execution is available.

---

# 33. Agent Task Format

When assigning implementation work to Claude Code or Codex, use this structure:

```text
TASK
<one concrete outcome>

CONTEXT
<why this exists>

SCOPE
<files/subsystems allowed to change>

REQUIREMENTS
- ...
- ...

NON-GOALS
- ...

CONSTRAINTS
- real-time safe
- no new dependency unless necessary
- preserve project schema

ACCEPTANCE CRITERIA
- ...
- ...

VALIDATION
- command
- command
```

Avoid prompts such as:

> Improve the audio engine.

Prefer:

> Implement a stereo delay processor with time, feedback, and wet/dry parameters, add deterministic DSP tests, and integrate it into the existing track effect chain without modifying the UI.

---

# 34. Required Agent Behavior Around Audio Code

For any DSP or audio-engine task, the coding agent must explicitly check:

```text
[ ] No allocation in the audio callback
[ ] No blocking lock in the audio callback
[ ] No file/network I/O in the audio callback
[ ] Parameters are safe across UI/audio threads
[ ] State is reset correctly
[ ] Sample-rate changes are handled
[ ] Channel count assumptions are explicit
[ ] Silence / zero-length edge cases are handled
[ ] Tests exist where deterministic testing is possible
```

---

# 35. First Recommended Implementation Tasks

Execute approximately in this order:

### Task 001
Bootstrap JUCE + CMake application.

### Task 002
Create audio-device manager and stable output callback.

### Task 003
Implement transport clock.

### Task 004
Implement oscillator + ADSR voice.

### Task 005
Implement playable synth with 8-voice polyphony.

### Task 006
Implement sample player.

### Task 007
Implement 16-step drum sequencer.

### Task 008
Implement project model and schema v1.

### Task 009
Implement audio recording.

### Task 010
Implement basic timeline and clip model.

### Task 011
Implement first-party filter.

### Task 012
Implement EQ.

### Task 013
Implement compressor.

### Task 014
Implement distortion.

### Task 015
Implement delay.

### Task 016
Implement reverb.

### Task 017
Implement effect chain.

### Task 018
Implement offline stereo WAV export.

---

# 36. Product Guardrail

At every milestone ask:

> Does this make it easier or more enjoyable to create sound?

If a feature mainly makes the application resemble a professional DAW but does not improve the core creative experience, postpone it.

The long-term advantage of this product should not be the number of features.

It should be:

> **How quickly a person can turn an impulse into sound.**

---

# 37. Working Name

Until branding is defined, use the neutral internal codename:

```text
Audio Playground
```

Do not bake the codename into persisted formats, public APIs, DSP parameter IDs, or identifiers that would be expensive to rename.
