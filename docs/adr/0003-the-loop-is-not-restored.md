---
status: accepted
---

# The Loop is not restored

The performance setup is written once a control settles and read back at launch. The Loop's frames are not part of that setup. They are a performance, the ring is wiped whenever its resolution changes, and saving them would mean a multi-gigabyte file for an image the next set does not continue. Launch starts from an empty Loop. Latch, Cue, and Export start off so a relaunch does not write by itself.
