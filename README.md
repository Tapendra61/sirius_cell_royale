# Cell Royale

<!-- Replace OWNER/REPO once the GitHub remote is set up. -->
[![Build](https://github.com/OWNER/REPO/actions/workflows/build.yml/badge.svg)](https://github.com/OWNER/REPO/actions/workflows/build.yml)

Agar.io-style game in raylib + C++20. Targets Windows, macOS, Linux, Android, iOS.

## Build

Default (Linux, macOS, or Windows with Visual Studio installed):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
./build/cell_royale            # macOS / Linux
.\build\Release\cell_royale.exe  # Windows / Visual Studio
```

Windows with **LLVM clang + Ninja** (no Visual Studio IDE / MSBuild required, just the VS Build Tools or VS Community install for the Windows SDK + MSVC runtime that LLVM clang links against):

```bat
cmake -B build -G Ninja ^
  -DCMAKE_C_COMPILER=clang ^
  -DCMAKE_CXX_COMPILER=clang++ ^
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
.\build\cell_royale.exe
```

> Note: this uses regular `clang.exe` from the [LLVM Windows installer](https://github.com/llvm/llvm-project/releases) — **not** `clang-cl.exe`. CMake auto-detects the Visual Studio install via the registry so the MSVC C++ runtime + Windows SDK are picked up without sourcing `vcvarsall.bat`.

Requires CMake 3.24+ and a C++20 compiler. Dependencies (raylib, raygui, mINI, ENet) are fetched automatically via CMake `FetchContent`.

Tested CI toolchains (see `.github/workflows/build.yml`):

- Ubuntu 22.04 / gcc + Unix Makefiles
- macOS 14 / AppleClang + Unix Makefiles
- Windows 2022 / MSVC cl.exe + Visual Studio generator
- Windows 2022 / LLVM clang.exe + Ninja generator

## Options

| Option | Default | Notes |
|---|---|---|
| `CR_ENABLE_DEV_TOOLS` | `ON` | Dev console + replay UI. Turn off for store builds. |
| `CR_ENABLE_NETWORK` | `OFF` | ENet-based multiplayer (Phase 10). |
| `CR_ENABLE_TELEMETRY` | `OFF` | Anonymized opt-in metrics. |
