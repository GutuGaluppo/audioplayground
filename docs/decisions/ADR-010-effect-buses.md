# ADR-010: Effect buses (aux sends)

**Status:** Accepted (2026-10-06). Amends ADR-009 ("no generic routing in the MVP" and the
constant-latency rule).

## Context
People want to share one reverb or delay between several tracks, and to put effects on a parallel
copy of a sound. The guide (§3, §9) keeps routing simple and lists a full mixer and an unlimited
routing graph as non-goals unless explicitly requested. This was requested, in a limited form.

## Decision
- **Aux buses only.** A project has up to 8 buses (`Project::buses`). A bus has a name, volume,
  pan, mute and the same fixed effect chain as a track (EQ → Compressor → Filter → Distortion →
  Delay → Reverb, `Bus::effects`). Buses never feed other buses, so there are no cycles, and a
  bus always outputs to the master.
- **Sends live on tracks** (`Track::sends`: bus id and level in dB, at most one per bus). A send is
  taken *after* the track's effects, volume, pan, mute and solo, so those control the send too.
  A send at the minimum level (−60 dB) is not stored.
- **Commands** (ADR-003): `AddBus`, `RemoveBus` (removes the sends that fed it; undo restores them
  in place), `RenameBus`, `SetBusVolume`, `SetBusPan`, `SetBusMute`, `SetBusEffect`,
  `SetTrackSend`. Volume, pan, effect and send gestures merge into one undo step. Ids (`BusId`)
  are never reused.
- **Persistence**: optional `buses` and `nextBusId` at the root, optional `sends` on tracks
  (`{bus, levelDb}`). Files without them load with no buses; files are written with them only when
  buses exist. Schema v1 is still a draft (ADR-006), so no migration. After release a change like
  this needs a version bump.
- **Engine.** Bus chains are `TrackChain`s kept across graphs by bus id, like track chains.
  `RenderGraph` carries resolved linear send gains per track and linear gains per bus. All buffers
  are allocated in `prepare`. Per block: tracks → chain → fader into the dry mix, and into each
  bus's input buffer scaled by the send; then each bus runs its chain and fader into the return
  mix. Send and bus gains ramp across the block, so changes never click. Buses always run, so a
  reverb or delay tail rings out after the sends stop or the transport does.
- **Constant latency, now two chains.** A path through a bus crosses two chains (38 samples each),
  so every path takes exactly two: tracks get one extra chain of delay after their mix, and sources
  without a track (metronome, test tone, an instrument without a track) get two. Dry and wet then
  stay aligned (a transparent bus doubles the signal exactly, with no comb filter) and nothing ever
  moves when a bus is added or removed. `Engine::getOutputLatency()` grows by 38 samples (0.8 ms
  at 48 kHz) for everything, always; offline renders and recordings compensate it as before.
- **UI.** Buses appear as rows under the tracks. Selecting one shows its effect chain in the
  effects panel; selecting a track shows a Sends card with one knob per bus. Selection is UI state.

## Consequences
- +0.8 ms output latency at 48 kHz, with or without buses.
- Memory: about 1 MB per bus (delay line and reverb), allocated when it first appears in a graph.
- CPU: an idle bus costs roughly an idle chain (switched-off effects cost nothing).
- Not done, on purpose: pre-fader sends, bus-to-bus routing, choosing a track's output, solo on
  buses, and send automation.
