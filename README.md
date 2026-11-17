# Video Looper

macOS desktop app: GPU video looper with live/file/webcam/shader sources, overdub blending + ADSR, optional FX, Ableton Link tempo, and `.mov` export.

**As-is map (architecture, loop, hotkeys, invariants):** see [`CONTEXT.md`](CONTEXT.md).

## Requirements

- macOS 11+
- CMake 3.24+
- Xcode command-line tools (OpenGL, AVFoundation, AppKit)
- Optional: [Ableton Link](https://github.com/Ableton/link) checkout next to this repo as `../link` (used by `build.sh`)

## Build & run

With Ableton Link (expects `../link`):

```bash
./build.sh
```

That configures with `-DUSE_ABLETON_LINK=ON`, builds `video_looper`, and launches:

`build/video_looper.app/Contents/MacOS/video_looper`

Without Link:

```bash
cmake -S . -B build
cmake --build build --target video_looper -j8
open build/video_looper.app
```

Override Link root: `LINK_ROOT=/path/to/link ./build.sh`

## Formatting

C++/ObjC++ under `app/src` follows `.clang-format` (uses Xcode's clang-format):

```bash
xcrun clang-format -i $(git ls-files 'app/src/*.cpp' 'app/src/*.hpp' 'app/src/*.mm')
```

## Layout

| Path | Contents |
|------|----------|
| `app/src/` | Application C++ / ObjC++ |
| `app/shaders/` | GLSL (hot-reloaded from source tree via `SHADER_DIR`) |
| `app/macos/Info.plist` | Bundle + camera privacy |
| `CMakeLists.txt` | Target, FetchContent GLFW/ImGui, optional Link |
| `CONTEXT.md` | Full as-is behavior & architecture |

## Quick controls

| Key | Action |
|-----|--------|
| Space | Hold to overdub into the loop |
| C | Hold to cue (preview overdub, no write) |
| U | Toggle UI |
| V | Load random file from videos folder (commit) |
| 1–9, 0 | Record opacity |
| `[` `]` | Blend mode |
| `'` `\` | FX mode |

ImGui **Controls** panel: Live (source, envelope, FX, disk record) and Settings (Link, tempo, preview).
