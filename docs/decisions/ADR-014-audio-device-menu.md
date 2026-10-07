# ADR-014: Audio device menu in the web UI

**Status:** Accepted (2026-10-07). Replaces the "temporary native dialog" in `WebUiHost`.

## Context
Choosing the audio output, input, sample rate and buffer size opened JUCE's native
`AudioDeviceSelectorComponent`, outside the app's look and unreachable by the UI's keyboard and
screen-reader work. A lost device (unplugged headphones, a driver restart) left silence behind a
banner (guide §24, §26 "device handling").

## Decision
- **The menu lives in the UI** ("Audio settings" in the status bar): output, input, sample rate,
  buffer size with its time in ms, the measured output latency, and an "Advanced…" button that
  still opens the native dialog (ASIO control panels, channel pairs, other device types).
- **Bridge.** Event `audio.devices` (device names of the current device type, the open output, the
  preferred input, the open device's sample rates and buffer sizes), sent on `app.ready` and on every
  device status change. Intents `audio.setOutput`, `audio.setInput`, `audio.setSampleRate`,
  `audio.setBufferSize`; names are checked against the device list, rates and sizes by the driver.
- **The input never opens from the menu** (ADR-008, guide §25): `audio.setInput` only records which
  input a later arm opens, or switches it if it is open now. Changing the output closes an open
  input; arming opens it again on the new pair.
- **Not while recording.** Device changes are refused with a notice during a take; the menu is
  re-synced to the device in use.
- **Lost device.** When the open device disappears, `AudioDeviceHost` moves to the system default
  once (a flag stops a failing default from looping) and the UI shows a notice naming the new
  device. If there is no device at all the existing banner stays.
- **Sample-rate changes** already work: the engine, sample loader and recorder re-prepare on
  `audioDeviceAboutToStart`.

## Alternatives
- *Keep the native dialog*: inconsistent look and not accessible from the UI.
- *Reopen the previous device automatically when it returns*: surprising in the middle of a
  session; left for later.

## Consequences
- Only the current device type's devices are listed (e.g. CoreAudio); switching type (WASAPI/ASIO)
  stays in "Advanced…".
- Recovery after an unplug needs real hardware to exercise; tests cover the list, the refusals and
  the UI, not the unplug itself.
