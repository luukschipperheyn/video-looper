#pragma once

// Everything that can hold the Overdub gate or Cue open this frame.
struct GateHolds {
	bool latch = false;    // the Latch
	bool space = false;    // Space on the Mac
	bool pad = false;      // the Overdub pad on the Web interface
	bool erase = false;    // the Erase pad
	bool cueLatch = false; // the latched Cue
	bool cueKey = false;   // C on the Mac
	bool cuePad = false;   // the Cue pad
};

struct GateSettings {
	bool envelope = true; // off: the gate is all or nothing
	float attack = 0;     // seconds
	float decay = 0;      // seconds
	float sustain = 1;    // 0..1
	float release = 0;    // seconds
	float opacity = 1;
	int blendMode = 0;
	bool bakeFx = false; // Bake FX is on and Program FX is running
};

enum class DubImage { Source, Black, ProgramFx };

// What the Loop gets this frame.
struct GateFrame {
	bool cue = false;     // show the Audition and leave the Loop alone
	bool overdub = false; // write the image into the Loop, and show the Audition
	float opacity = 0;    // of the write, and of the Audition's overlay
	// Black is Erase. ProgramFx replaces the Loop's frames with the Program FX output whether or not overdub
	// is set. The Audition overlays the Source for ProgramFx.
	DubImage image = DubImage::Source;
	int blendMode = 0;         // Mix (0) for Black
	bool restartVideo = false; // a Source overdub or Cue just opened; a Video in Trigger mode restarts
	float envelope = 0;        // 0..1
	int stage = 0;             // 0 idle, 1 attack, 2 decay, 3 sustain, 4 release
	bool gate = false;         // a Source overdub hold is down: Latch, Space, or the pad. Not Erase.
	bool erase = false;        // the image is black, including an Erase release tail
};

// Decides once per frame whether the Loop is written, with what, and how strongly.
//
// The gate is open while Latch, Space, the Overdub pad, or Erase is held. With the envelope on, the write
// follows the ADSR and carries on through the release tail after the gate closes. Cue runs the envelope but
// never writes, and a Cue cancels a release tail's write. Erase wins over the other holds while it is held and
// keeps the image black through its own release tail; a Source hold or Cue takes the image back at once.
// A Commit writes the Source at full strength and bypasses the envelope and Erase.
class OverdubGate {
public:
	GateFrame step(const GateHolds& holds, const GateSettings& settings, bool commitActive, float dtSeconds);

private:
	void runEnvelope(const GateSettings& settings, bool held, float dt);

	float envelope = 0;
	int stage = 0;
	bool heldBefore = false;
	bool sourceBefore = false;
	bool blackLatched = false;
	bool writeThroughRelease = false;
};
