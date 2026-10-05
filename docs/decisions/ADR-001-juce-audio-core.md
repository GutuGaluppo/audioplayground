# ADR-001: JUCE 9 as the audio core

**Status:** Accepted (2026-10-05)

## Context
The engine needs reliable audio/MIDI device access on macOS and Windows, audio file decoding and encoding, and a native shell that can host a WebView UI. Writing these per platform is expensive and error-prone.

## Decision
- Use JUCE **9.0.3**, pinned by version and SHA-256 in `cmake/Dependencies.cmake`.
- JUCE is used under its **commercial licence, Starter tier (free)**, through the JUCE 9 EULA (account on juce.com). Starter has an annual revenue cap. Re-check the current cap and upgrade (Indie/Pro) before crossing it.
- JUCE is confined to `apps/desktop` and to platform I/O adapters. `core/` DSP and model code must stay usable without JUCE GUI modules, which keeps the WASM path open (ADR-002).
- Bundle ID: `io.github.gutugaluppo.audioplayground` (reverse-DNS of the GitHub namespace). It can still be changed freely before the first public release. After that, changing it resets user permissions and preferences. If a product domain is acquired, switch to `<tld>.<domain>.<app>` before release.

## Alternatives
- **Custom stack (RtAudio + libsndfile + native shell):** more ownership, but much more platform code and a slower MVP.
- **AGPLv3 licence:** free, but the whole application would have to be published under AGPLv3.

## Consequences
- Upgrading JUCE means changing the version, the hash and `docs/THIRD_PARTY_LICENSES.md`, then reading `BREAKING_CHANGES.md`.
- Licence tier limits (revenue) must be re-checked before commercial release.
