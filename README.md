# Cell Royale

<!-- Replace OWNER/REPO once the GitHub remote is set up. -->
[![Build](https://github.com/OWNER/REPO/actions/workflows/build.yml/badge.svg)](https://github.com/OWNER/REPO/actions/workflows/build.yml)

Agar.io-style game in raylib + C++20. Targets Windows, macOS, Linux, Android, iOS.

## Build

```bash
cmake -B build
cmake --build build
./build/cell_royale
```

Requires CMake 3.24+ and a C++20 compiler. Dependencies (raylib, raygui, mINI) are fetched automatically via CMake `FetchContent`.

## Options

| Option | Default | Notes |
|---|---|---|
| `CR_ENABLE_DEV_TOOLS` | `ON` | Dev console + replay UI. Turn off for store builds. |
| `CR_ENABLE_NETWORK` | `OFF` | ENet-based multiplayer (Phase 10). |
| `CR_ENABLE_TELEMETRY` | `OFF` | Anonymized opt-in metrics. |
