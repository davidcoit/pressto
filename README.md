# Pressto Compressor (JUCE VST3)

Buildable on Linux, macOS, and Windows with CMake presets. The devcontainer still works for Linux builds; you can now build locally on any platform without inheriting container paths.

## Prerequisites
- CMake ≥ 3.22
- C++17 toolchain (Ninja + clang/gcc on Linux, Xcode/clang on macOS, MSVC 2022 on Windows)
- VST3 SDK (set `VST3_SDK_DIR` or `VST3_SDK_PATH` if you have a local copy). If not provided, CMake will fetch `vst3sdk` automatically when `PRESSTO_FETCH_VST3_SDK` is ON (default).
- On Windows, run from a "x64 Native Tools for VS 2022" prompt.

## Build (recommended presets)
Run commands from the repo root:

```bash
# Linux (Debug/Release)
cmake --preset linux-debug   && cmake --build --preset linux-debug
cmake --preset linux-release && cmake --build --preset linux-release

# macOS (universal binary: arm64 + x86_64)
cmake --preset macos-universal && cmake --build --preset macos-universal

# Windows (multi-config Visual Studio generator)
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
```

Artifacts live under `build/<preset>/PresstoCompressor_artefacts/`.

## Notes
- If you previously configured inside the Docker container, remove the old `build/` directory to avoid cached container paths (`CMakeCache.txt`).
- Override VST3 location: `cmake --preset linux-release -DVST3_SDK_DIR="C:/SDKs/vst3sdk"`.
- The devcontainer still uses the existing Dockerfile; it will transparently use the Linux presets.
