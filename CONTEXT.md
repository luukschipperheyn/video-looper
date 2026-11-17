# Video Looper

A live VJ instrument that loops video and lets the performer layer a live source onto the loop. Architecture and as-is behavior live in [`docs/architecture.md`](docs/architecture.md).

## Language

### Writing into the loop

**Overdub**:
Writing the live Source into the loop, shaped by the blend mode, opacity, and ADSR envelope.
_Avoid_: Record, recording

**Overdub gate**:
The momentary control that opens an Overdub while held.
_Avoid_: Record gate, rec

**Latch**:
The toggle that holds the Overdub gate open without a finger on it; it overdubs immediately, it does not wait for a trigger.
_Avoid_: Arm Record, arm

**Cue**:
Showing the Audition without writing into the loop, to try a Source before overdubbing it.
_Avoid_: Monitor, pre-listen

**Commit**:
Writing one whole Video into the Loop at full opacity from its start to its end, bypassing the envelope. The Video is picked at random from the Videos folder.
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

### Getting video out

**Export**:
Saving rendered video to a `.mov` file on disk.
_Avoid_: Record, recording, disk record
