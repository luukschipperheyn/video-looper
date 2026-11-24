# Video Looper

A live VJ instrument that loops video and lets the performer layer a live source onto the loop. Architecture and as-is behavior live in [`docs/architecture.md`](docs/architecture.md).

## Language

### Writing into the loop

**Overdub**:
Writing the live Source into the loop, shaped by the blend mode, opacity, and ADSR envelope.
_Avoid_: Record, recording

**Erase**:
An Overdub that writes black instead of the Source. It mixes toward black, shaped by opacity and the ADSR, and ignores the blend mode.
_Avoid_: Clear, wipe

**Overdub gate**:
The momentary control that opens an Overdub while held.
_Avoid_: Record gate, rec

**Latch**:
The toggle that holds the Overdub gate open without a finger on it; it overdubs immediately, it does not wait for a trigger.
_Avoid_: Arm Record, arm

**Cue**:
Showing the Audition without writing into the loop, to try a Source before overdubbing it.
_Avoid_: Monitor, pre-listen

**Panic**:
The control that releases the Latch, the latched Cue, and a held Overdub gate or Cue.
_Avoid_: reset, clear

**Commit**:
Writing one whole Video into the Loop at full opacity from its start to its end, bypassing the envelope.
_Avoid_: Load random file

**Bake FX**:
Writing the FX output into the loop instead of the live Source.
_Avoid_: Commit FX, FX commit

### Images

**Source**:
The live input image before it touches the Loop: a Video, webcam, shader, or black.
_Avoid_: Preview, input

**Video**:
A video file that can be a Source.
_Avoid_: Clip, file

**Videos folder**:
The folder whose Videos Prev, Next, and random Commit choose from.
_Avoid_: Clip folder, video bank, bank

**Folder list**:
The Videos folders prepared before a set, in order. One is current; the others are waiting.
_Avoid_: Video bank, bank, playlist

**Loop**:
The repeating sequence of frames the audience sees.
_Avoid_: Ring, buffer

**Audition**:
The loop with the Source laid over it in the Overdub's blend mode — what an Overdub would produce.
_Avoid_: Preview

**Program**:
The image the audience sees: the loop as presented, including FX.
_Avoid_: Output, main view

"Preview" is a verb or UI label (to preview the Audition), never a term for one of these images.

### FX

**Source FX**:
FX on the Source, after framing and before the Audition or an Overdub.
_Avoid_: input FX, pre-FX

**Repeat**:
An FX that keeps the image and stamps extra copies in one direction. Distance is the gap, as a fraction of the frame height. Angle is the direction; zero points right and increasing angle turns upward. The count is how many extra copies. Black stays black.
_Avoid_: echo, tile, array, kaleido

**Grid**:
An FX that keeps the image and stamps extra copies on two axes. The first axis is the Repeat direction. The second stands square to it. Distance is the gap on both axes. The count is how many extra copies along each axis. Black stays black.
_Avoid_: tile, array, kaleido

### Getting video out

**Export**:
Saving rendered video to a `.mov` file on disk.
_Avoid_: Record, recording, disk record

### Control

**Control**:
One named value a performer sets, such as the opacity or the tempo. Each has a default and, if it is a number, a range.
_Avoid_: param, parameter, setting

**Instrument**:
The looper as the thing that is played. Every control surface (the Mac controls, the hotkeys, the Web interface) asks the instrument for a change; none of them changes a control by itself.
_Avoid_: app, engine, backend

**Web interface**:
The page the app serves for live control from a browser.
_Avoid_: iPad page
