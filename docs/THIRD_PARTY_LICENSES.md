# Third-Party Licenses

Every third-party library, algorithm, sample, impulse response, preset source and bundled asset must be listed here **before** it is merged (guide §30). "Free" does not mean commercially redistributable.

## Shipped in the application

| Component | Version | Licence | Pinned by | Notes |
|---|---|---|---|---|
| JUCE | 9.0.3 | JUCE 9 EULA, Starter tier (free, revenue-capped). Dual-licensed AGPLv3, not used | SHA-256 in `cmake/Dependencies.cmake` | See ADR-001. JUCE's own vendored deps: `JUCE.spdx.json` in the JUCE source |
| nlohmann/json | 3.12.0 | MIT | SHA-256 in `cmake/Dependencies.cmake` | Project file parsing (fuzzed upstream on OSS-Fuzz and here) |
| Microsoft Edge WebView2 SDK (Windows) | 1.0.3485.44 | BSD-3-Clause | SHA-256 in `cmake/Dependencies.cmake` | Static loader linked into the Windows app |
| React / React DOM | 19.3.0 | MIT | `ui/package.json` + `pnpm-lock.yaml` | |

## Algorithms (our own implementations of published designs)

| Algorithm | Source | Licence | Where |
|---|---|---|---|
| Freeverb structure and tunings (8 combs + 4 all-passes, stereo spread 23) | Jezar at Dreampoint, 2000 | Public domain | `core/src/fx/Reverb.cpp` (no Freeverb code copied) |
| SVF shelf/bell formulation | Andrew Simper (Cytomic), technical paper | Published maths, no code | `core/include/ap/dsp/EqBand.h` |
| Compressor gain computer and smoothing | Giannoulis, Massberg & Reiss, JAES 2012 | Published maths, no code | `core/src/fx/Compressor.cpp` |

## Development and test only (not shipped)

| Component | Version | Licence |
|---|---|---|
| Catch2 | 3.9.1 | BSL-1.0 |
| TypeScript | 6.0.3 | Apache-2.0 |
| Vite / @vitejs/plugin-react | 8.3.2 / 6.1.1 | MIT |
| Vitest, jsdom, Testing Library | 5.0.3 / 30.1.2 / 16.3.3 | MIT |
| ESLint, typescript-eslint, eslint-plugin-react-hooks | 10.12.0 / 8.71.0 / 7.1.1 | MIT |
| Prettier | 3.9.9 | MIT |

## Audio content
None yet.
