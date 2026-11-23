# Video Looper — as-is context

macOS OpenGL/ImGui app that loops live/file/webcam/shader video into a dual-bank GPU frame ring, overdubs with blend modes + ADSR, optional post FX, Ableton Link tempo, and disk export to `.mov`. **No audio path.** This document describes behavior as implemented in source (static read); it is not a product roadmap.

Entry: `app/src/main.cpp` → `App::run()` in `app/src/App.cpp`.

---

## Usage context

Target use is **live VJ performance**: steady frame rate and no hitches or lost loops mid-set outweigh offline quality. No formal memory or resolution budget is set yet.

GPU memory is dominated by the ring: `2 banks × 120 frames × fbW × fbH × 4 bytes`. Framebuffer size is in physical pixels, so a 1280×720 window on Retina (2560×1440) costs ~3.5 GB; fullscreen 4K costs ~8 GB. Any feature that adds per-frame GPU storage multiplies against this.

Test clips for the File source and folder/commit paths: `~/Movies/video-datasets/vids` (~100 `.mp4`, mixed sizes up to ~100 MB; `.json` manifests in the folder are skipped by the scan). No `.mkv`/`.avi` samples, so those extensions are untested.

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
| `app/src/io/PageCache.*` | Opens the Videos on the Web interface's current page ahead of a tap |
| `app/src/io/AVFFileSource.*` / `AVFWebcamSource.*` | File decode / camera → GL texture |
| `app/src/io/AVFRecorder.*` | BGRA frames → `.mov` |
| `app/src/io/VideoSource.hpp` | `SourceKind { Shader, File, Black, Webcam }`, `IVideoSource` |
| `app/src/gl/FxPipeline.*` | Feedback / Diff / Chroma / Kaleido / Blur / Repeat / Grid; one pipeline on the Program, one on the Source; ping-pong targets; hot-reload |
| `app/src/gl/Shader.*`, `FullscreenQuad.*`, `util/GlUtils.hpp` | GL helpers |
| `app/src/ui/ParamBus.*` | String-keyed bool/float/int/string bag (no schema validation) |
| `app/src/setup/SetupStore.*` | Performance setup saved when a control settles and restored at launch |
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

File and webcam frames are drawn into the Source, then that image is what an Overdub writes. **Preserve aspect** (`video_aspect`, default on) fits them with contain: the image fills the loop's width or its height. Off stretches them to the loop frame first. **Black bars** (`video_bars`, default on) paints the unused side black; off leaves it transparent, so the Loop shows through and an Overdub does not paint it. The Web interface Source trackpad drags with one finger to translate. A pinch moves by the midpoint and scales and rotates (`video_tx`, `video_ty`, `video_sx`, `video_sy`, `video_rot`, identity at 0, 0, 1, 1, 0). With Preserve aspect on, a pinch scales both axes together. Shader and Black ignore the transform and fill the loop frame. The switches are on the Mac Source section and the Web interface Source section.

**Folder / random Video** (`SourceController`):

- `videos_folder` scanned for `.mp4/.mov/.m4v/.mkv/.avi`  
- `V` / “Load Random File” picks a Video from the current Videos folder and loads it as the Source, the same way Prev and Next do. It does not Commit.
- Prev/Next cycle folder list and load immediately (clears commit state)

**Web interface grid** (`app/web/index.html`): Select loads the tapped Video as the Source. Dub does that and Commits it, so the Video is written into the Loop at full opacity from its start to its end and stays the Source. The page sends the filenames on screen; `PageCache` opens those Videos on a background thread so a tap swaps a ready one in. A Video that is not ready yet still loads, and a Dub Commits when that load swaps in.

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

Two chains, each an `FxPipeline`. **Program FX** (`fx_enabled`) runs on the present path when not cueing or overdubbing. **Source FX** (`source_fx_enabled`) runs on the framed Source; Cue, the Audition, an Overdub, and the inset preview all use that image. Source FX does not change the Loop until an Overdub writes it. Each chain keeps its own previous frame, so feedback on one does not leak into the other. Source FX adds two full-frame targets, not ring frames.

| Mode | Name | Notes |
|------|------|--------|
| 0 | Feedback | Program FX default |
| 1 | Difference | |
| 2 | Chromatic | |
| 3 | Kaleido | |
| 4 | Blur | separable X then Y, after the feedback pass |
| 5 | Repeat | extra copies along one step. Distance is `param1` as a fraction of the frame height. Angle is `param2` in turns (0 points right, increasing turns upward). Count is `fx_count` or `source_fx_count`, 0..16 extra copies. Copies combine with `fx_repeat_blend` or `source_fx_repeat_blend` (the Overdub blend modes, default Lighten). Black does not cover; a copy landing on empty black is placed as itself. Samples outside the frame are black. |
| 6 | Grid | the same stamp on that step and on the perpendicular step (`turns + 0.25`). Count is extra copies along each axis, 0..6, so a count of 4 is a 5×5 grid. Same distance, blend, and black-does-not-cover rule as Repeat. |

Program params: `fx_param1/2/3`. Source params: `source_fx_param1/2/3`. Source FX defaults to Repeat, distance 0.12, angle 0, count 4. `fx_commit` + Program `fx_enabled` writes the previous Program FX output into the ring instead of the live Source. Shaders hot-reload via `HotReload` watches under `app/shaders/`. The Program pipeline reads the watches; the Source pipeline reloads from that same result, because a watch reports a change only once per edit.

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
| `'` / `\` | Prev/next `fx_mode` (0–6) |
| `V` | Random file commit trigger |
| Space | Hold = record gate |
| `C` | Hold = cue (monitor only) |

Hotkeys ignore ImGui keyboard capture (`wantKb` hardcoded false).

---

## ParamBus defaults (init)

Notable defaults from `App.cpp` ~120–162: `show_ui`, `gain_mix=1`, `record_blend_opacity=1`, `bpm=120`, `quantum=4`, `read_speed_mult=1`, `tempo_coupled=true`, Link off, `source_kind=Shader`, `video_bars=true`, `env_enabled=true` (A/D/S/R 0.05/0.20/1.0/0.30), `fx_enabled=false`, `record_fps=60`, preview on inset scale 0.3, playheads on.

Keys used elsewhere but not all initialized at startup (rely on get defaults): e.g. `file_run_mode`, `file_loop_start`, `file_loop_length`.

## Saved setup

`~/Library/Application Support/video-looper/setup.json` is written 0.2 s after the setup stops changing, and again on quit. Launch applies it after the defaults, so a missing or broken file leaves the defaults. Restored fields: blend, opacity, blend gain, envelope, FX, Source FX, Source kind and transform, black bars, preserve aspect, webcam, video loop / pause / run mode / loop range, current Video, Folder list, tempo, whether the Web interface is on, export path and export fps. Latch, latched Cue, and Export start off. Link, the preview window, playheads, Mac UI visibility, the URL token, and the window are not in the file. A folder that cannot be read is dropped. If the current Video cannot be opened, the Source stays a Video, nothing is loaded, and the Web interface keeps the load failure on its snapshot. The Loop's frames are not restored (ADR-0003). A restored Video's first frame is the start of its saved loop range.

`web-use.jsonl` in that same folder is an append-only record of Web interface gestures, one JSON object per line: `at` (UTC), `e` (`set`, `hold`, `release`, `open`, `close`, `chain`, `tap`, `act`), `name`, and `v` when the gesture has a value. A fader, blend swipe, transform, or envelope drag is one `set` when the finger lifts, with the value it landed on. A held Overdub gate or Cue is one `hold` and one `release`, not the repeats that keep the pad down. Opening the ADSR/opacity layer or Setup is `open` / `close`. The file keeps its newest lines once it passes 4 MB. It is not restored into the instrument and it is not sent off the Mac.

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
3. **Commit** forces recording-active writes at opacity 1 and bypasses ADSR; ends when the reader is exhausted while the Video is not looping. A tick that holds the current frame does not end it.  
4. **Resize invalidates** ring contents (full `setup` destroy/recreate) and aborts disk recording. Accepted for now by the owner; don't change it as a side effect.  
5. **Read interpolation** — present mixes `idx0`/`idx1` with `gain_mix`; write path uses integer slot indices.  
6. **FX present vs commit** — Program FX is a present-path effect unless `fx_commit` bakes the last Program FX texture into the loop. Source FX is on the framed Source before the Audition and an Overdub.  
7. **Preview vs main** — inset/separate preview shows `auditionTexB` (loop + live overlay); debug toggle shows the back-bank slot instead.

---

## Quick file anchors

| Concern | Start here |
|---------|------------|
| Frame orchestration | `app/src/App.cpp` |
| Ring write/swap math | `app/src/looper/LooperEngine.cpp` |
| Dual-bank storage | `app/src/buffer/RingBuffer.cpp` |
| Live UI | `app/src/ui/ControlsPanel.cpp` |
| Saved setup | `app/src/setup/SetupStore.cpp` |
| Sources / commit | `app/src/io/SourceController.cpp` |
| FX modes | `app/src/gl/FxPipeline.cpp` |
| Link stub vs real | `app/src/link/LinkBridge.cpp` |
| Build graph | `CMakeLists.txt`, `build.sh` |
