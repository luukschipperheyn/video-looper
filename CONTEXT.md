# Video Looper — as-is context

macOS OpenGL/ImGui app that loops live/file/webcam/shader video into a dual-bank GPU frame ring, overdubs with blend modes + ADSR, optional post FX, Ableton Link tempo, and disk export to `.mov`. **No audio path.** This document describes behavior as implemented in source (static read); it is not a product roadmap.

Entry: `app/src/main.cpp` → `App::run()` in `app/src/App.cpp`.

---

## Usage context

Target use is **live VJ performance**: steady frame rate and no hitches or lost loops mid-set outweigh offline quality. No formal memory or resolution budget is set yet.

GPU memory is dominated by the ring: `2 banks × 120 frames × fbW × fbH × 4 bytes`. Framebuffer size is in physical pixels, so a 1280×720 window on Retina (2560×1440) costs ~3.5 GB; fullscreen 4K costs ~8 GB. Any feature that adds per-frame GPU storage multiplies against this.

---

## Platforms / deps / build

| Item | As implemented |
|------|----------------|
| OS | macOS only (`.mm` AVFoundation/AppKit, OpenGL.framework, `MACOSX_BUNDLE`) |
| Min OS | 11.0 (`app/macos/Info.plist`) |
| GL | OpenGL 4.1 core, forward-compat (`App.cpp` window hints) |
| Window | GLFW 3.4 (FetchContent), vsync on, default 1280×720 @ 60 refresh hint |
| UI | Dear ImGui `docking` branch + glfw/opengl3 backends; viewports enabled |
| Video I/O | AVFoundation / CoreMedia (`AVFFileSource`, `AVFWebcamSource`, `AVFRecorder`) — not FFmpeg |
| Shaders | `SHADER_DIR` compile def → `${CMAKE_SOURCE_DIR}/app/shaders` |
| Link | Optional `USE_ABLETON_LINK`; default OFF in CMake, ON in `build.sh` via sibling `../link` |
| C++ | C++20 |

Build/run commands: see `README.md`. Bundle camera privacy: `NSCameraUsageDescription` + Continuity Camera flag in `Info.plist`.

---

## Module map

| Path | Role |
|------|------|
| `app/src/App.cpp` | GLFW/ImGui lifecycle, ParamBus defaults, hotkeys, main loop orchestration, audition FBOs, disk capture |
| `app/src/buffer/RingBuffer.*` | Dual-bank GPU textures/FBOs (`frameCount` frames × 2 banks); front = read, back = write |
| `app/src/looper/LooperEngine.*` | Read-head advance, multi-slot write/copy to back bank, bank swap on wrap, playhead overlay |
| `app/src/io/SourceController.*` | Source kind routing, preview tex/FBO, folder scan, random **commit**, prev/next file |
| `app/src/io/AVFFileSource.*` / `AVFWebcamSource.*` | File decode / camera → GL texture |
| `app/src/io/AVFRecorder.*` | BGRA frames → `.mov` |
| `app/src/io/VideoSource.hpp` | `SourceKind { Shader, File, Black, Webcam }`, `IVideoSource` |
| `app/src/gl/FxPipeline.*` | Feedback / Diff / Chroma / Kaleido / Blur post; ping-pong targets; hot-reload |
| `app/src/gl/Shader.*`, `FullscreenQuad.*`, `util/GlUtils.hpp` | GL helpers |
| `app/src/ui/ParamBus.*` | String-keyed bool/float/int/string bag (no schema validation) |
| `app/src/ui/ControlsPanel.*` | ImGui **Live** + **Settings** tabs |
| `app/src/clock/Clock.hpp` | Local beat accumulator from BPM × dt |
| `app/src/link/LinkBridge.*` | Real Ableton Link when `USE_ABLETON_LINK`; else local stub |
| `app/src/util/HotReload.*` | mtime watches for shader reload |
| `app/src/util/MacFileDialog.*` | Native open/save/directory dialogs |
| `app/shaders/*` | Vert/frag used by blit, blend, test pattern, FX |

---

## Main-loop stages

Order inside `App::run`’s `while` (`App.cpp` ~192–647), each frame:

1. **Poll** GLFW events  
2. **Hotkeys** (edge-triggered; see UI + hotkeys) — `WantCaptureKeyboard` is forced `false`, so keys always apply  
3. **Preview window** create/destroy from `preview_window`  
4. **Resize** — if framebuffer size changes: rebuild ring (120 frames), source, FX, looper, audition targets; **stop** disk recorder if active  
5. **Hot-reload** shaders / FX  
6. **Disk record edge** — start/stop `AVFRecorder` from `record_enabled` + `record_path`  
7. **`source.tickAndGrab`** — webcam/file upload; Shader/Black filled into preview FBO in App  
8. **Clock / Link** — tempo → `readFps` (optionally coupled to BPM)  
9. **`looper.advanceRead`** — fractional read head → `idx0/idx1/mix/wraps`  
10. **Record/cue + ADSR** — gates, envelope, `recordingActive`, `writeOpacity`  
11. **`writeAndSwap`** unless **cue** (cue = monitor only, no ring write)  
12. **Present** — cue/record audition path, else FX or plain loop  
13. **Preview audition** composite into `auditionTexB` when preview visible and not cue/recording  
14. **Disk capture** — if recording: rebuild audition → `glReadPixels` BGRA → vertical flip → `appendBGRAFrame` at `record_fps`  
15. **Inset / separate preview** blit of `auditionTexB` (or back-bank debug)  
16. **ImGui** ControlsPanel + optional playhead overlay  
17. **Swap** buffers (+ ImGui viewports)

Ring size is fixed at **120** frames at current framebuffer resolution (`ring.setup(fbw, fbh, 120)`).

---

## Sources

`SourceKind` (`VideoSource.hpp`):

| Kind | Preview content |
|------|-----------------|
| Shader (0) | `test_pattern.frag` into preview FBO (`uTime`, `uResolution`) |
| File (1) | `AVFFileSource` — loop, pause, normalized loop start/length, Free vs Trigger run mode |
| Black (2) | Clear preview FBO to black |
| Webcam (3) | `AVFWebcamSource` by `webcam_index` |

**Folder / random commit** (`SourceController`):

- `videos_folder` scanned for `.mp4/.mov/.m4v/.mkv/.avi`  
- `V` / “Load Random File” → `commitRequested` → pick file, set `commit_active`, force `file_loop=false`, write into ring at full opacity until file ends (no new frames + non-looping) then restore loop setting  
- Prev/Next cycle folder list and load immediately (clears commit state)

**File Trigger mode** (`file_run_mode == 1`): on rising edge of record gate or cue, `restartFileAtLoopStart()` if source is File.

---

## Looper / ring

**RingBuffer** — two banks of `N` RGBA8 textures+FBOs. Reads always from **front**; writes bind **back** at index. `swapBanks()` flips front/back (called once per read-head wrap count from `LooperEngine`).

**Read** (`advanceRead`): advances `readFrame` by `readFps * dt`; returns floor/ceil indices + fractional mix; counts wraps when crossing `N`.

**Write** (`writeAndSwap`):

- Fills back-bank slots from last write index up to current `idx0` (handles wrap-induced full-ring rewrite)  
- If **recording** (or commit): blend front tex at slot with **preview** (or blit FX prev if `fx_commit`) using `record_blend_mode` + `writeOpacity`  
- If not recording: **copy** front → back once per slot (`backWritten` gate) so the back bank stays a coherent next loop  
- After processing, `lastWriteIdx = idx0`; for each wrap, `swapBanks()` and clear `backWritten`

**Cue** (`C` held or `cue_active`): presents audition (loop + live overlay) but **skips** `writeAndSwap`.

**Record gate**: `arm_record` UI **or** Space held. With envelope on, `shouldRecordOnRelease` latches so release still writes while envelope > 0. Cue clears that latch while active.

**ADSR** (`env_*`, default on): phases 0 idle → 1 attack → 2 decay → 3 sustain → 4 release. Disabled or `commit_active` → opacity is binary (gate/commit) rather than enveloped. `writeOpacity = record_blend_opacity * envOrGate`.

**Blend modes** (0–11): Mix, Add, Multiply, Screen, Overlay, Difference, Lighten, Darken, Hard Light, Soft Light, Color Dodge, Color Burn — shader `texture_blend_two.frag` via `uMode`.

**Playheads**: ImGui foreground bar — cyan read, red write index (`show_playheads`).

---

## FX

`FxPipeline::process` when `fx_enabled` and not in cue/record present path:

| Mode | Name | Notes |
|------|------|--------|
| 0 | Feedback | default |
| 1 | Difference | |
| 2 | Chromatic | |
| 3 | Kaleido | |
| 4 | Blur | separable X then Y |

Params: `fx_param1/2/3`. `fx_commit` + `fx_enabled` writes previous FX output into the ring instead of live preview blend.

Shaders hot-reload via `HotReload` watches under `app/shaders/`.

---

## Clock / Link

- Local `Clock`: `beats += (bpm/60)*dt`, phase = `fmod(beats, quantum)`.  
- `tempo_coupled` (default **true** in ParamBus init): `readFps = 60 * (bpm/120) * read_speed_mult`.  
- Else: `readFps = 60 * read_speed_mult`.  
- With Link enabled: tempo (and start/stop/playing flags) come from `LinkBridge`; BPM written back into ParamBus.  
- Without `USE_ABLETON_LINK`: stub advances local beats; `getNumPeers()==0`; start/stop sync is no-op; `isAvailable()==false`.

---

## Record / export (disk)

Separate from looper “record into ring”:

- UI: Enable Recording + path (or Save As…) + Record FPS  
- Rising edge starts `AVFRecorder` at framebuffer size; empty path refuses start  
- Each period: read `auditionFboB` as BGRA, flip Y, append  
- Resize while recording stops recorder and clears `record_enabled`  
- Captured image is the **audition composite** (loop + live overlay at record opacity/mode), not necessarily the on-screen FX view

---

## UI + hotkeys

**ImGui window** `Controls` — tabs Live / Settings (`ControlsPanel.cpp`). Hidden when `show_ui` false.

**Live:** disk recording; Arm Record / Cue; Source (+ file folder/commit/run mode/loop range); Blending; ADSR; FX; FPS readout.

**Settings:** UI/preview options; Ableton Link + peers; BPM/quantum/tempo couple/read speed; debug bank letters + Link SDK compiled-in flag.

### Hotkeys (`App.cpp`)

| Key | Action |
|-----|--------|
| `U` | Toggle `show_ui` |
| `1`–`9`, `0` | `record_blend_opacity` = 0.1…0.9, 1.0 |
| `-` / `=` | Prev/next `source_kind` — **only through Black** (`=` stops at Black; Webcam not reachable via hotkey) |
| `[` / `]` | Prev/next `record_blend_mode` (0–11) |
| `'` / `\` | Prev/next `fx_mode` (0–4) |
| `V` | Random file commit trigger |
| Space | Hold = record gate |
| `C` | Hold = cue (monitor only) |

Hotkeys ignore ImGui keyboard capture (`wantKb` hardcoded false).

---

## ParamBus defaults (init)

Notable defaults from `App.cpp` ~120–162: `show_ui`, `gain_mix=1`, `record_blend_opacity=1`, `bpm=120`, `quantum=4`, `read_speed_mult=1`, `tempo_coupled=true`, Link off, `source_kind=Shader`, `env_enabled=true` (A/D/S/R 0.05/0.20/1.0/0.30), `fx_enabled=false`, `record_fps=60`, preview on inset scale 0.3, playheads on.

Keys used elsewhere but not all initialized at startup (rely on get defaults): e.g. `file_run_mode`, `file_loop_start`, `file_loop_length`.

---

## Stubs / conditionals

By design:

| Observation | Where |
|-------------|--------|
| Ableton Link optional; stub without SDK | `LinkBridge.*`, CMake `USE_ABLETON_LINK` |
| No audio capture/playback/sync | no audio sources under `app/` |
| `RingBuffer::commitWrite` no-op | bank flip only on wrap |
| macOS / OpenGL only | CMake Apple frameworks |

---

## Known bugs

Confirmed unintentional by the owner. Fix rather than preserve.

| Bug | Where |
|-----|-------|
| `-`/`=` source cycle stops at Black; Webcam unreachable by hotkey | `App.cpp` `=` capped at `SourceKind::Black` |
| Hotkeys fire while typing in ImGui text fields | `App.cpp` `wantKb = false` |
| Preview ignores the ADSR envelope (always full record opacity) | `App.cpp` preview audition pass |
| Disk recording captures the audition composite, not the presented main view (FX missing) | `App.cpp` capture reads `auditionFboB` |
| Editing `file_path` while a file is playing does not reload it | `SourceController::ensureSource` only starts when `!fileActive` |
| `[rec]` printf on every captured frame | `App.cpp` capture loop |
| Placeholder bundle id `com.example.video-looper` | `Info.plist` |

---

## Invariants & edge cases

1. **Bank coherence** — non-recording path copies unread-back slots so after a wrap the new front is a complete loop; recording overdubs into those slots.  
2. **Cue never mutates the ring** — audition only.  
3. **Commit** forces recording-active writes at opacity 1 and bypasses ADSR; ends when file stops producing frames while non-looping.  
4. **Resize invalidates** ring contents (full `setup` destroy/recreate) and aborts disk recording.  
5. **Read interpolation** — present mixes `idx0`/`idx1` with `gain_mix`; write path uses integer slot indices.  
6. **FX present vs commit** — live FX is a present-path effect unless `fx_commit` bakes last FX texture into the loop.  
7. **Preview vs main** — inset/separate preview shows `auditionTexB` (loop + live overlay); debug toggle shows the back-bank slot instead.

---

## Quick file anchors

| Concern | Start here |
|---------|------------|
| Frame orchestration | `app/src/App.cpp` |
| Ring write/swap math | `app/src/looper/LooperEngine.cpp` |
| Dual-bank storage | `app/src/buffer/RingBuffer.cpp` |
| Live UI | `app/src/ui/ControlsPanel.cpp` |
| Sources / commit | `app/src/io/SourceController.cpp` |
| FX modes | `app/src/gl/FxPipeline.cpp` |
| Link stub vs real | `app/src/link/LinkBridge.cpp` |
| Build graph | `CMakeLists.txt`, `build.sh` |
