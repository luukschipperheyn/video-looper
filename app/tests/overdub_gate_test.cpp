#include "looper/OverdubGate.hpp"

#include <cmath>
#include <cstdio>
#include <initializer_list>

static int failures = 0;

static void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) ++failures;
}

static bool near(float a, float b) {
	return std::fabs(a - b) < 1e-4f;
}

static GateHolds pad() {
	GateHolds h;
	h.pad = true;
	return h;
}
static GateHolds erase() {
	GateHolds h;
	h.erase = true;
	return h;
}
static GateHolds cuePad() {
	GateHolds h;
	h.cuePad = true;
	return h;
}

// attack 0.2, decay 0.2 down to sustain 0.5, release 0.4; a frame is 0.1 s.
static GateSettings adsr() {
	GateSettings s;
	s.attack = 0.2f;
	s.decay = 0.2f;
	s.sustain = 0.5f;
	s.release = 0.4f;
	s.opacity = 1.0f;
	s.blendMode = 6;
	return s;
}

static GateSettings noEnvelope() {
	GateSettings s;
	s.envelope = false;
	s.opacity = 0.5f;
	s.blendMode = 3;
	return s;
}

int main() {
	const float dt = 0.1f;
	const GateHolds none;

	{
		OverdubGate gate;
		GateFrame f = gate.step(none, adsr(), false, dt);
		check(!f.overdub && !f.cue && f.stage == 0 && f.opacity == 0.0f, "1. idle writes nothing");

		for (GateHolds h : {GateHolds{true}, GateHolds{false, true}, pad()}) {
			OverdubGate one;
			GateFrame g = one.step(h, adsr(), false, dt);
			check(g.overdub && g.gate && !g.cue, "2. Latch, Space, and the pad each open the gate");
		}
		GateHolds both;
		both.space = true;
		both.pad = true;
		OverdubGate two;
		two.step(both, adsr(), false, dt);
		both.pad = false;
		check(two.step(both, adsr(), false, dt).stage != 4, "2. Space plus a pad stays open when the pad releases");
	}

	{
		OverdubGate gate;
		GateFrame f = gate.step(pad(), adsr(), false, dt);
		check(f.stage == 1 && near(f.envelope, 0.5f) && near(f.opacity, 0.5f) && f.overdub,
		      "3. attack rises and the write follows it");
		f = gate.step(pad(), adsr(), false, dt);
		check(f.stage == 2 && near(f.envelope, 1.0f), "3. attack reaches full and hands over to decay");
		f = gate.step(pad(), adsr(), false, dt);
		check(f.stage == 2 && near(f.envelope, 0.75f), "3. decay falls toward sustain");
		f = gate.step(pad(), adsr(), false, dt);
		check(f.stage == 3 && near(f.envelope, 0.5f), "3. decay lands on sustain");
		f = gate.step(pad(), adsr(), false, dt);
		check(f.stage == 3 && near(f.opacity, 0.5f) && f.image == DubImage::Source && f.blendMode == 6,
		      "3. sustain holds, writing the Source in the blend mode");

		f = gate.step(none, adsr(), false, dt);
		check(f.stage == 4 && f.overdub && !f.gate && near(f.envelope, 0.25f),
		      "4. after release the write carries on through the tail");
		f = gate.step(none, adsr(), false, dt);
		check(f.stage == 0 && !f.overdub && f.envelope == 0.0f, "4. the tail ends and the write stops");
	}

	{
		GateSettings s = adsr();
		s.opacity = 0.5f;
		OverdubGate gate;
		gate.step(pad(), s, false, dt);
		GateFrame f = gate.step(pad(), s, false, dt);
		check(near(f.opacity, 0.5f) && near(f.envelope, 1.0f), "5. opacity scales the envelope");

		GateSettings instant = adsr();
		instant.attack = 0;
		instant.decay = 0;
		instant.release = 0;
		OverdubGate snap;
		f = snap.step(pad(), instant, false, dt);
		check(f.stage == 3 && near(f.envelope, 1.0f), "5. zero attack and decay jump to sustain's stage at full");
		f = snap.step(pad(), instant, false, dt);
		check(near(f.envelope, 0.5f), "5. and settle on sustain the frame after");
		snap.step(none, instant, false, dt);
		f = snap.step(none, instant, false, dt);
		check(f.stage == 0 && !f.overdub, "5. zero release closes without a tail");
	}

	{
		OverdubGate gate;
		GateFrame f = gate.step(cuePad(), adsr(), false, dt);
		check(f.cue && !f.overdub && !f.gate && near(f.opacity, 0.5f),
		      "6. Cue runs the envelope for the Audition and does not write");
		f = gate.step(none, adsr(), false, dt);
		check(!f.cue && !f.overdub && f.stage == 4 && f.envelope > 0.0f, "6. a Cue's release tail does not write");

		OverdubGate tail;
		for (int i = 0; i < 4; ++i)
			tail.step(pad(), adsr(), false, dt);
		tail.step(none, adsr(), false, dt);
		tail.step(cuePad(), adsr(), false, dt);
		f = tail.step(none, adsr(), false, dt);
		check(!f.overdub && f.envelope > 0.0f, "6. a Cue during an Overdub's tail cancels the rest of the write");
	}

	{
		OverdubGate gate;
		GateFrame f = gate.step(pad(), adsr(), false, dt);
		check(f.restartVideo, "7. opening the gate asks a triggered Video to restart");
		check(!gate.step(pad(), adsr(), false, dt).restartVideo, "7. holding it does not ask again");
		OverdubGate cue;
		check(cue.step(cuePad(), adsr(), false, dt).restartVideo, "7. opening Cue asks too");
		OverdubGate er;
		check(!er.step(erase(), adsr(), false, dt).restartVideo, "7. Erase does not");
	}

	{
		OverdubGate gate;
		GateFrame f = gate.step(erase(), adsr(), false, dt);
		check(f.overdub && f.erase && !f.gate && f.image == DubImage::Black && f.blendMode == 0 &&
		          near(f.opacity, 0.5f),
		      "8. Erase writes black as Mix, shaped by the envelope");
		GateSettings baked = adsr();
		baked.bakeFx = true;
		GateHolds with = erase();
		with.latch = true;
		f = gate.step(with, baked, false, dt);
		check(f.image == DubImage::Black && f.gate, "8. Erase wins over Latch and over Bake FX while held");
		with.erase = false;
		f = gate.step(with, adsr(), false, dt);
		check(f.image == DubImage::Source && !f.erase && f.blendMode == 6,
		      "8. releasing Erase with the gate still held gives the image back at once");

		OverdubGate tail;
		for (int i = 0; i < 4; ++i)
			tail.step(erase(), adsr(), false, dt);
		f = tail.step(none, adsr(), false, dt);
		check(f.overdub && f.erase && f.image == DubImage::Black, "8. the release tail of an Erase stays black");
		f = tail.step(none, adsr(), false, dt);
		check(!f.erase && !f.overdub, "8. the black image is dropped once the envelope is idle");

		OverdubGate cued;
		for (int i = 0; i < 4; ++i)
			cued.step(erase(), adsr(), false, dt);
		cued.step(none, adsr(), false, dt);
		f = cued.step(cuePad(), adsr(), false, dt);
		check(!f.erase && f.image == DubImage::Source && !f.overdub, "8. Cue takes the image back and does not write");
	}

	{
		OverdubGate gate;
		GateFrame f = gate.step(pad(), noEnvelope(), false, dt);
		check(f.overdub && near(f.opacity, 0.5f) && f.blendMode == 3,
		      "9. without the envelope the gate is all or nothing");
		f = gate.step(none, noEnvelope(), false, dt);
		check(!f.overdub && f.opacity == 0.0f, "9. and closes with no tail");
		gate.step(erase(), noEnvelope(), false, dt);
		f = gate.step(none, noEnvelope(), false, dt);
		check(!f.erase && !f.overdub, "9. a disabled envelope has no black tail");
		f = gate.step(cuePad(), noEnvelope(), false, dt);
		check(f.cue && !f.overdub && f.opacity == 0.0f, "9. Cue alone shows the Audition at no opacity");
	}

	{
		GateSettings s = adsr();
		s.opacity = 0.25f;
		OverdubGate gate;
		GateFrame f = gate.step(none, s, true, dt);
		check(f.overdub && near(f.opacity, 0.25f) && f.stage == 0 && f.image == DubImage::Source,
		      "10. a Commit writes the Source at the opacity with no envelope");
		f = gate.step(erase(), s, true, dt);
		check(f.image == DubImage::Source && !f.erase, "10. a Commit still writes the Video while Erase is held");
		f = gate.step(cuePad(), s, true, dt);
		check(f.cue && f.overdub, "10. Cue during a Commit is still reported, so the Loop is left alone");
	}

	{
		GateSettings baked = adsr();
		baked.bakeFx = true;
		OverdubGate gate;
		GateFrame f = gate.step(none, baked, false, dt);
		check(f.image == DubImage::ProgramFx && !f.overdub,
		      "11. Bake FX names the Program FX image with the gate closed");
		f = gate.step(pad(), baked, false, dt);
		check(f.image == DubImage::ProgramFx && f.overdub, "11. and with it open");
	}

	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
