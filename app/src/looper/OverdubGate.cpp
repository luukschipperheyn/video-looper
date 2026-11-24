#include "looper/OverdubGate.hpp"

#include <algorithm>

void OverdubGate::runEnvelope(const GateSettings& settings, bool held, float dt) {
	float attack = std::max(settings.attack, 0.0f);
	float decay = std::max(settings.decay, 0.0f);
	float sustain = std::clamp(settings.sustain, 0.0f, 1.0f);
	float release = std::max(settings.release, 0.0f);

	if (held && !heldBefore) stage = 1;
	if (!held && heldBefore && stage != 0) stage = 4;
	switch (stage) {
	case 0:
		envelope = 0.0f;
		break;
	case 1:
		if (attack > 0.0f) envelope += dt / attack;
		if (attack <= 0.0f || envelope >= 1.0f) {
			envelope = 1.0f;
			stage = decay > 0.0f ? 2 : 3;
		}
		break;
	case 2:
		if (decay > 0.0f) envelope -= (1.0f - sustain) * (dt / decay);
		if (decay <= 0.0f || envelope <= sustain) {
			envelope = sustain;
			stage = 3;
		}
		break;
	case 3:
		envelope = sustain;
		if (!held) stage = 4;
		break;
	case 4:
		if (release > 0.0f) envelope -= dt / std::max(release, 1e-6f);
		if (release <= 0.0f || envelope <= 0.0f) {
			envelope = 0.0f;
			stage = 0;
		}
		break;
	}
}

GateFrame OverdubGate::step(const GateHolds& holds, const GateSettings& settings, bool commitActive, float dt) {
	bool sourceGate = holds.latch || holds.space || holds.pad;
	bool cue = holds.cueLatch || holds.cueKey || holds.cuePad;
	bool gate = sourceGate || holds.erase;
	bool held = gate || cue;

	if (cue) writeThroughRelease = false;
	if (gate) writeThroughRelease = true;

	if (!commitActive && settings.envelope) runEnvelope(settings, held, dt);

	GateFrame out;
	out.restartVideo = (sourceGate || cue) && !sourceBefore;
	sourceBefore = sourceGate || cue;
	heldBefore = held;

	if (holds.erase)
		blackLatched = true;
	else if (sourceGate || cue || !settings.envelope || stage == 0)
		blackLatched = false;
	bool black = blackLatched && !commitActive;

	float strength = envelope;
	if (commitActive)
		strength = 1.0f;
	else if (!settings.envelope)
		strength = gate ? 1.0f : 0.0f;

	out.cue = cue;
	out.overdub = commitActive || (settings.envelope ? (writeThroughRelease && envelope > 0.0f) : gate);
	out.opacity = settings.opacity * strength;
	out.image = black ? DubImage::Black : settings.bakeFx ? DubImage::ProgramFx : DubImage::Source;
	out.blendMode = black ? 0 : settings.blendMode;
	out.envelope = envelope;
	out.stage = stage;
	out.gate = sourceGate;
	out.erase = black;
	return out;
}
