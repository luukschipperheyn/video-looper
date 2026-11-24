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
| `app/src/App.cpp` | GLFW/ImGui lifecycle, main loop orchestration, export capture |
| `app/src/control/ControlTable.hpp` | Every control once: name, type, default, range, whether the Web interface sees or sets it, whether it is saved |
| `app/src/control/Instrument.*` | Carries out what a performer asks for from any surface: set a control (clamped), Panic, Prev/Next/random Video, load, Dub, choose a folder or the webcam. It records `SourceStatus` into the controls. `SourcePort` is what it asks of the Source |
| `app/src/control/Hotkeys.*` | One frame of the keys that are down, and whether a text field has focus. A rising key asks the Instrument for a change. Returns the Overdub hold and the Cue hold |
| `app/src/ui/ParamBus.*` | The current value of every control; a fresh bus holds the table's defaults |
| `app/src/looper/OverdubGate.*` | Decides per frame whether the Loop is written, with which image, blend mode and opacity: holds, ADSR, release tail, Erase, Cue, Commit bypass. Pure |
| `app/src/buffer/RingBuffer.*` | Dual-bank GPU textures/FBOs (`frameCount` frames × 2 banks); front = read, back = write |
| `app/src/looper/LoopPlan.*` | Pure frame of the Loop: read head, which back-bank slots are filled (Overdub, copy forward, or Bake FX), and when the banks swap |
| `app/src/looper/LooperEngine.*` | Runs that plan against the banks, then the playhead overlay. The picture path draws each slot |
| `app/src/gl/Compositor.*`, `Frame.cpp`, `PictureChoice.*` | The picture path. Keeps the framed Source, the Audition, and the Program as named images. Owns the blit and blend shaders, the quad, the frame size, those images, the picture texture (`PicturePort`), the Draw texture, and the export readback. `choosePictures` names the image the window, the preview, and Export ask for, and the opacity of the Audition. Tested in `app/tests/picture_choice_test.cpp`. `initFrame` and `resizeFrame` set that size on the Loop and both FX pipelines. `reloadShaders` polls the watches once. `drawSource` returns the Source FX stamp |
| `app/src/io/SourceController.*` | The life of the Source. Requests come from the Instrument. It opens a Video or webcam off the main thread, swaps it in with its first frame, and **Commit**s a Video until it ends. It writes no controls. No GL or AVFoundation; decoders come from a `DecoderFactory` and pictures go to a `PicturePort`. Tested with fake decoders in `app/tests/source_controller_test.cpp` |
| `app/src/io/FolderList.*` | The Folder list. Videos folders in order, one of them current, each with the Videos captured when it was added. Selecting one does not read the disk |
| `app/src/io/PageCache.*` | Opens the Videos on the Web interface's current page ahead of a tap |
| `app/src/io/AVFFileSource.*` / `AVFWebcamSource.*` | The AVFoundation `VideoDecoder` / `CameraDecoder`: file decode / camera → GL texture. `AVFDecoders.hpp` is their factory |
| `app/src/io/AVFRecorder.*` | BGRA frames → `.mov` |
| `app/src/io/VideoSource.hpp` | `SourceKind { Shader, File, Black, Webcam, Draw }`; the ports `VideoDecoder`, `CameraDecoder`, `DecoderFactory`, and `PicturePort` |
| `app/src/io/DrawImage.mm` | Draw PNG → straight-alpha BGRA, bottom row first |
| `app/src/gl/FxPipeline.*` | Feedback / Diff / Chroma / Kaleido / Blur / Repeat / Grid; one pipeline on the Program, one on the Source; ping-pong targets; each owns its quad. The Source FX stamp is the placement the picture path used for the framed Source |
| `app/src/gl/Shader.*`, `FullscreenQuad.*`, `util/GlUtils.hpp` | GL helpers |
| `app/src/remote/RemoteSession.*` | HTTP + WebSocket server for the Web interface, holds, thumbnails |
| `app/src/remote/RemoteApply.*` | The Web interface adapter: turns one message into an Instrument call and a reject frame; builds the snapshot |
| `app/src/setup/SetupStore.*` | Performance setup (the controls the table marks Saved, plus the Folder list) saved when a control settles and restored at launch |
| `app/src/ui/ControlsPanel.*` | ImGui **Live** + **Settings** tabs; every change goes through the Instrument |
| `app/src/clock/Clock.hpp` | Local beat accumulator from BPM × dt |
| `app/src/link/LinkBridge.*` | Real Ableton Link when `USE_ABLETON_LINK`; else local stub |
| `app/src/util/HotReload.*` | mtime watches for shader reload |
| `app/src/util/MacFileDialog.*` | Native open/save/directory dialogs |
| `app/shaders/*` | Vert/frag used by blit, blend, test pattern, FX |

---

## Main-loop stages

Order inside `App::run`’s `while`, each frame:

1. **Poll** GLFW events  
2. **Hotkeys** (see UI + hotkeys). They run from the keys that are down. ImGui `WantCaptureKeyboard` means a text field has focus, and then no hotkey fires and Space and C do not open the Overdub gate or Cue  
3. **Preview window** create/destroy from `preview_window`  
4. **Resize** — if framebuffer size changes: `resizeFrame` rebuilds the ring (120 frames), both FX pipelines, the looper playhead, and the picture targets, in that order; **stop** disk recorder if active  
5. **Hot-reload** — `reloadShaders` polls the watches once. Every program that uses a changed shader reloads, including both FX pipelines  
6. **Disk record edge** — start/stop `AVFRecorder` from `record_enabled` + `record_path`  
7. **`source.tick`, then `Instrument::record`** — a swap lands in the controls. The tick uploads a webcam or Video frame into the Compositor's picture texture; the `Compositor` then draws Shader, Black, or the placed picture as the framed Source  
8. **Clock / Link** — tempo → `readFps` (optionally coupled to BPM)  
9. **Web interface messages, then the Overdub gate** — each drained message goes through `applyRemote` to the `Instrument`; `OverdubGate::step` takes the holds and returns this frame's write: overdub or not, opacity, image, blend mode, Cue  
10. **`looper.step`** — one call advances the read head, plans every back-bank fill and any swap, then does the texture work. Cue is decided here: the plan writes nothing and does not swap. The picture's textures are the front bank from before a swap  
11. **Pictures** — `choosePictures` names the window, the preview, and Export. The picture path keeps the Program (Program FX when it is on, otherwise the Loop) and the Audition at the opacity an Overdub would write  
12. **Present** — the window asks for the Audition while cueing or overdubbing, including Bake FX, and for the Program otherwise  
13. **Export** — while Export is on, `Compositor::read` of the Program (BGRA, top row first) goes to `appendBGRAFrame` at `record_fps`  
14. **Inset and second window** — each asks for the Audition. The debug toggle shows the back-bank slot in the inset instead  
15. **ImGui** ControlsPanel + optional playhead overlay  
16. **Swap** buffers (+ ImGui viewports)

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
| Draw (4) | The picture from the Web interface canvas, stretched over the loop frame |

File and webcam frames are drawn into the Source, then that image is what an Overdub writes. **Preserve aspect** (`video_aspect`, default on) fits them with contain: the image fills the loop's width or its height. Off stretches them to the loop frame first. **Black bars** (`video_bars`, default on) paints the unused side black; off leaves it transparent, so the Loop shows through and an Overdub does not paint it. The Web interface Source trackpad drags with one finger, a mouse button, or a trackpad click to translate. A pinch moves by the midpoint and scales and rotates (`video_tx`, `video_ty`, `video_sx`, `video_sy`, `video_rot`, identity at 0, 0, 1, 1, 0). With Preserve aspect on, a pinch scales both axes together. A trackpad pinch does the same: Safari reports scale and clockwise rotation, and Chromium and Firefox report that pinch as a wheel event with the ctrl key set. A wheel without the ctrl key does not scale or rotate. The page itself stays at scale 1: a pinch anywhere and a double tap do not zoom the page. Alt-drag scales: dragging up by the frame height doubles it, and with Preserve aspect off a sideways drag scales width the same way. Shift-drag or the right button rotates: a drag across the frame width is half a turn, and dragging right turns the image clockwise. Shader, Black, and Draw ignore the transform and fill the loop frame. The switches are on the Mac Source section and the Web interface Source section.

**Videos folder**:

- The Folder list holds every Videos folder and the Videos captured when that folder was added. Mac Choose Folder, the Videos Folder field, and Add Folder each scan the folder once for `.mp4`, `.mov`, `.m4v`, `.mkv`, and `.avi`, add it, and make it current. Choosing a folder already on the list uses those stored Videos. The frame loop publishes their filenames. A load, a Dub, and a primed page take paths from that list.
- `V` / “Load Random File” picks a Video from the current Videos folder and loads it as the Source, the same way Prev and Next do. It does not Commit.
- Prev and Next step through the Videos on the current Videos folder and load immediately.

**Web interface grid** (`app/web/index.html`): The folder chips are the Folder list. The chip whose path equals `videos_folder` is the current one. The grid is the filenames of that folder's Videos. Select loads the tapped Video as the Source. Dub does that and Commits it, so the Video is written into the Loop at full opacity from its start to its end and stays the Source. The page sends the filenames on screen; `PageCache` opens those Videos on a background thread so a tap swaps a ready one in. A Video that is not ready yet still loads, and a Dub Commits when that load swaps in.

**Draw** replaces that grid with a canvas. Pen, eraser, six colors, two brush sizes, and clear. The ground is transparent or black. Strokes stay in the page when the Source changes away and back, until the page reloads; they are not in the setup file. While Draw is the Source the page sends the picture as a PNG on a binary WebSocket frame, throttled while the finger moves and once when it lifts. A newer picture replaces one still waiting. The app stretches it over the loop frame. Transparent pixels do not paint the Loop; a black ground does. An empty Draw, before the first picture, is transparent.

**File Trigger mode** (`file_run_mode == 1`): on rising edge of the Source overdub gate or Cue, `Instrument::restartVideo` restarts the Video when the Source is a Video. Erase does not restart it.

---

## Looper / ring

The Loop is one step per frame (`LooperEngine::step`). `planLoop` is the pure part: it advances the read head, says which slot is on screen, and lists the back-bank fills and the swaps. The engine then blits or blends those slots. App does not hand the read indices back in.

**RingBuffer** — two banks of `N` RGBA8 textures+FBOs. Reads use the **front** bank. Writes bind the **back** bank. `swapBanks()` flips them when the plan says the pass is complete.

**Read.** The head moves by `readFps * dt`. The picture is the slot the head was on at the start of the step (`idx0`), mixed toward the next slot (`idx1`). Those textures are taken from the front bank before any swap in the same step.

**Write.** Each back-bank slot is filled once per pass:

- **Bake FX** replaces the slot with the Program FX picture  
- **Overdub** blends the Source over the front slot (Erase blends black; a Commit is an Overdub at full opacity)  
- otherwise the slot is **copied forward** from the front bank, so the next pass keeps the Loop  

On the frame the head crosses the loop point, the plan fills every slot of that pass that is not filled yet, including the slots past the one on screen, then swaps. A frame that jumps more than one pass fills each of those passes once and swaps once per pass. The slots the head has already entered in the new pass are filled on later frames.

**Cue** (`C` held, latched Cue, or the Cue pad): the head still advances, and the plan fills nothing and does not swap, so the Loop on screen is unchanged. A loop point crossed during Cue is owed. When Cue ends, the unfilled slots of that pass are copied forward and the owed swap happens. The banks are a complete Loop again.

**Record gate**: Latch (`arm_record`) **or** Space held **or** the Overdub pad **or** Erase. With the envelope on, `shouldRecordOnRelease` latches so release still writes while the envelope is above 0. Cue clears that latch while active.

**Erase** (Web interface hold under Overdub): opens that same gate, so the ADSR runs as an Overdub, and the image is opaque black rather than the Source. The blend mode is bypassed: the write is Mix, so the envelope fades the Loop toward black. Opacity still scales it. Source FX is not applied to the black. While Erase is held it wins over Latch, Space, and the Overdub pad. Releasing it while one of those still holds the gate switches the image back to the Source immediately. Releasing it with the gate closed keeps writing black through the release tail. Cue still skips the ring write. Commit still writes the Video.

**ADSR** (`env_*`, default on): phases 0 idle → 1 attack → 2 decay → 3 sustain → 4 release. Disabled or a Commit in progress → opacity is binary (gate/commit) rather than enveloped. The write opacity is `record_blend_opacity` × envelope or gate. All of this section lives in `OverdubGate` and is tested frame by frame in `app/tests/overdub_gate_test.cpp`.

**Blend modes** (0–11): Mix, Add, Multiply, Screen, Overlay, Difference, Lighten, Darken, Hard Light, Soft Light, Color Dodge, Color Burn — shader `texture_blend_two.frag` via `uMode`.

**Playheads**: ImGui foreground bar — cyan read, red write index (`show_playheads`).

---

## FX

Two chains, each an `FxPipeline`. **Program FX** (`fx_enabled`) is drawn into the Program image each frame. The window shows that image when it is not cueing or overdubbing. **Source FX** (`source_fx_enabled`) runs on the framed Source; Cue, the Audition, an Overdub, and the inset preview all use that image. Source FX Repeat and Grid are the exception: they stamp the whole Video or webcam picture at its placement, so a picture that runs past the frame is not cropped before it is repeated. The frame still clips whatever falls outside it. Source FX does not change the Loop until an Overdub writes it. Each chain keeps its own previous frame, so feedback on one does not leak into the other. Source FX adds two full-frame targets, not ring frames.

| Mode | Name | Notes |
|------|------|--------|
| 0 | Feedback | Program FX default |
| 1 | Difference | |
| 2 | Chromatic | |
| 3 | Kaleido | |
| 4 | Blur | separable X then Y, after the feedback pass |
| 5 | Repeat | extra copies along one step. Distance is `param1` as a fraction of the frame height. Angle is `param2` in turns (0 points right, increasing turns upward). Count is `fx_count` or `source_fx_count`, 0..16 extra copies. Copies combine with `fx_repeat_blend` or `source_fx_repeat_blend` (the Overdub blend modes, default Lighten). Black does not cover; a copy landing on empty black is placed as itself. Samples outside the frame are black. |
| 6 | Grid | the same stamp on that step and on the perpendicular step (`turns + 0.25`). Count is extra copies along each axis, 0..6, so a count of 4 is a 5×5 grid. Same distance, blend, and black-does-not-cover rule as Repeat. |

Program params: `fx_param1/2/3`. Source params: `source_fx_param1/2/3`. Source FX defaults to Repeat, distance 0.12, angle 0, count 4. `fx_commit` + Program `fx_enabled` writes the previous Program FX output into the ring instead of the live Source. Shaders hot-reload via `HotReload` watches under `app/shaders/`. `reloadShaders` polls those watches once and reloads the picture-path programs and both FX pipelines from that result. The Source pipeline reloads from the Program pipeline's result. A vertex-shader edit reaches every program that uses it. Repeat and Grid on Source FX stamp the placement `drawSource` returned for that frame.

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

### Hotkeys

| Key | Action |
|-----|--------|
| `U` | Toggle `show_ui` |
| `1`–`9`, `0` | `record_blend_opacity` = 0.1…0.9, 1.0 |
| `-` / `=` | Equal cycles Shader, File, Black, Webcam and stops there. Draw stays out of that cycle. Minus steps back through that cycle, and while Draw is the Source it returns to Black. Both stops are `Instrument::stepSourceKind`. |
| `[` / `]` | Prev/next `record_blend_mode` (0–11) |
| `'` / `\` | Prev/next `fx_mode` (0–6) |
| `V` | random Video. It does not Commit. |
| Space | Hold opens the Overdub gate |
| `C` | Hold opens Cue |

---

## Controls

`app/src/control/ControlTable.hpp` lists every control once: name, type, default, range, whether the Web interface sees it (`Web::Hidden` / `Read` / `Write`), and whether it is in the saved setup (`Keep::Session` / `Saved` / `StartsOff`). A fresh `ParamBus` holds those defaults, so getters take no fallback. The schema the page reads, the snapshot, the saved setup's fields, and the Mac sliders' ranges all come from the table. To add a control, add its row.

A change from the Mac controls, a hotkey, or the Web interface goes through `Instrument`, which clamps numbers to the table's range and does what follows from the change: Preserve aspect copies scale x onto scale y, choosing Shader, Black, or Draw asks the Source to show that, choosing a Video loads `file_path` when one is set, editing `file_path` loads that Video, choosing the webcam or its index starts a webcam load, loop and pause and the loop range are sent as playback, a new tempo goes to Link when Link is on, Export with no path is refused. When a load swaps in, `Instrument::record` writes the Source kind, the Video path, and the webcam index. The Web interface adapter (`applyRemote`) adds the read-only check. Commit state is not a control; `SourceController::committing()` reports it.

## Saved setup

`~/Library/Application Support/video-looper/setup.json` is written 0.2 s after the setup stops changing, and again on quit. Launch applies it after the defaults, so a missing or broken file leaves the defaults. Launch restore of a Video, webcam, Shader, Black, or Draw is `Instrument::restoreSource`. Restored fields: blend, opacity, blend gain, envelope, FX, Source FX, Source kind and transform, black bars, preserve aspect, webcam, video loop / pause / run mode / loop range, current Video, Folder list, tempo, whether the Web interface is on, export path and export fps. A Commit plays its Video once and does not change video loop or pause. Latch, latched Cue, and Export start off (`Keep::StartsOff` in the control table). Link, the preview window, playheads, Mac UI visibility, the URL token, and the window are not in the file. A folder that cannot be read is dropped. If the current Video cannot be opened, the Source stays a Video, nothing is loaded, and the Web interface keeps the load failure on its snapshot. The Loop's frames are not restored (ADR-0003). A restored Video's first frame is the start of its saved loop range.

`web-use.jsonl` in that same folder is an append-only record of Web interface gestures, one JSON object per line: `at` (UTC), `e` (`set`, `hold`, `release`, `open`, `close`, `chain`, `tap`, `act`), `name`, and `v` when the gesture has a value. A fader, blend swipe, transform, or envelope drag is one `set` when the finger lifts, with the value it landed on. A trackpad pinch is one `set` when the gesture ends. A held Overdub gate, Erase, or Cue is one `hold` and one `release`, not the repeats that keep the pad down. Opening the ADSR/opacity layer or Setup is `open` / `close`. The file keeps its newest lines once it passes 4 MB. It is not restored into the instrument and it is not sent off the Mac.

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
| `[rec]` printf on every captured frame | `App.cpp` capture loop |
| Placeholder bundle id `com.example.video-looper` | `Info.plist` |

---

## Invariants & edge cases

1. **Bank coherence** — every back-bank slot is filled once per pass before the banks swap, including slots the wrap frame passes and slots Cue skipped. Copy keeps the Loop; Overdub and Bake FX replace it.  
2. **Cue never mutates the Loop** — the plan writes nothing and does not swap while Cue is on.  
3. **Commit** forces recording-active writes at opacity 1 and bypasses ADSR. The Video plays once for that Commit, and `file_loop` and `file_paused` stay the performer's. It ends when the reader is exhausted. A tick that holds the current frame does not end it.  
4. **Resize invalidates** ring contents (full `setup` destroy/recreate) and aborts disk recording. Accepted for now by the owner; don't change it as a side effect.  
5. **Read interpolation** — present mixes `idx0`/`idx1` with `gain_mix`; write path uses integer slot indices.  
6. **FX present vs commit** — Program FX is kept in the Program image each frame. The window shows the Program when not cueing or overdubbing. Bake FX writes the previous Program image into the loop. Source FX is on the framed Source before the Audition and an Overdub.  
7. **Preview vs main** — the window asks for the Audition while cueing or overdubbing, and for the Program otherwise. The inset and the second window ask for the Audition at the opacity an Overdub would write. The debug toggle shows the back-bank slot in the inset instead.

---

## Quick file anchors

| Concern | Start here |
|---------|------------|
| Frame orchestration | `app/src/App.cpp` |
| What a control is, its default and range | `app/src/control/ControlTable.hpp` |
| What setting a control or pressing a button does | `app/src/control/Instrument.cpp` |
| When the Loop is written and with what | `app/src/looper/OverdubGate.cpp` |
| Which slots a frame fills, and when the banks swap | `app/src/looper/LoopPlan.cpp` |
| Source, Audition, Program, and which image is asked for | `app/src/gl/Compositor.cpp`, `app/src/gl/PictureChoice.cpp` |
| Applying that plan to the banks | `app/src/looper/LooperEngine.cpp` |
| Dual-bank storage | `app/src/buffer/RingBuffer.cpp` |
| Live UI | `app/src/ui/ControlsPanel.cpp` |
| Saved setup | `app/src/setup/SetupStore.cpp` |
| Sources / commit | `app/src/io/SourceController.cpp` |
| FX modes | `app/src/gl/FxPipeline.cpp` |
| Link stub vs real | `app/src/link/LinkBridge.cpp` |
| Build graph | `CMakeLists.txt`, `build.sh` |
