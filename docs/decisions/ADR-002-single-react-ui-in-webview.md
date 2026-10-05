# ADR-002: One UI — React + TypeScript in a WebView

**Status:** Accepted (2026-10-05)

## Context
The development guide implies two UIs: native JUCE on desktop and React on web. That doubles UI work and lets the UX diverge.

## Decision
- Build one UI in `ui/` (React + TypeScript strict).
- Desktop hosts it in JUCE's `WebBrowserComponent` (WKWebView / WebView2). The UI is served from **embedded resources through a resource provider**, never from a localhost server or a remote URL.
- The UI never processes audio. High-rate data (meters, playhead, waveforms) reaches `<canvas>` as throttled snapshots (30–60 Hz), outside React state.
- Performance input (QWERTY, MIDI, pads) is handled by the native core directly so bridge latency never reaches the sound.

## Security requirements
- Strict CSP (`default-src 'self'`), injected at build time.
- No navigation outside the app; devtools off in release builds.
- Native functions are an explicit allowlist; every payload is validated in C++.

## Alternatives
- **Native JUCE UI on desktop + React on web:** rejected because it means two UIs.
- **Native JUCE UI everywhere:** rejected because the web target would need a separate UI anyway.

## Consequences
- Requires the WebView2 runtime on Windows (preinstalled on Windows 11).
- Bridge message types must come from a single schema (ADR-003).
