# 2026-11-01 — CMake for macOS and Windows

Compared with the previous changelog (2026-11-01-fix-analysis-findings.md).

## Changes
- CMake now builds on Windows with Visual Studio 2022. Presets: `vs2022-x64`, `vs2022-win32`.
- CMake can now make an Xcode project on macOS. Preset: `xcode`.
- The macOS/Linux presets (`debug`, `release`, `asan`, `tsan`) now use Ninja, not Makefiles.
- macOS release builds are universal (arm64 + x86_64). The Xcode preset is universal in Debug too.
- macOS builds run on macOS 12.2 or later, like the Xcode project in `prj/`.
- On Windows the library uses the static C runtime (`/MT`, `/MTd`), like the VS2019 projects.
- New build and test presets. Every platform builds with `cmake --build --preset <name>`.
- Minimum CMake is now 3.15 for the CMakeLists, 3.21 for the presets.

## Docs
- README shows the build steps for each platform.
- CLAUDE.md lists the new presets.

## Not tested
- The Windows presets. No Windows machine was available.
