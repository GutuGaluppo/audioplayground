# Audio Playground

A desktop-first creative audio playground: **open → play → record → shape → combine → export**.
Working codename; see `docs/DEVELOPMENT_GUIDE.md` §37.

## Requirements
- macOS 13+ with Xcode command line tools, or Windows with Visual Studio 2022+
- CMake ≥ 3.25
- Node ≥ 22 and pnpm 11

## Build and run
```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
open "build/dev/apps/desktop/ap_desktop_artefacts/Debug/Audio Playground.app"
```

UI (standalone, in a browser):
```bash
pnpm install
pnpm ui:dev
```

## Documentation
- [Development guide](docs/DEVELOPMENT_GUIDE.md)
- [Implementation plan](docs/IMPLEMENTATION_PLAN.md)
- [Architecture decisions](docs/decisions/)
- [Third-party licences](docs/THIRD_PARTY_LICENSES.md)
- [Agent instructions](AGENTS.md)
