#include "looper/LoopPlan.hpp"

#include <cmath>

LoopPlan planLoop(LoopMark& mark, int frames, float dtSeconds, float readFps, const LoopWish& wish) {
	LoopPlan plan;
	const int n = frames;
	if (n <= 0) return plan;
	if (static_cast<int>(mark.backWritten.size()) != n) mark.backWritten.assign(n, 0);

	float newRead = mark.readFrame + readFps * dtSeconds;
	int wraps = 0;
	if (newRead >= static_cast<float>(n)) {
		wraps = static_cast<int>(std::floor(newRead / static_cast<float>(n)));
		newRead = std::fmod(newRead, static_cast<float>(n));
		mark.lastWriteIdx = -1;
	}
	float wrapped = mark.readFrame;
	if (wrapped < 0.f) wrapped += static_cast<float>(n);
	const int idx0 = static_cast<int>(std::floor(wrapped)) % n;
	plan.read.idx0 = idx0;
	plan.read.idx1 = (idx0 + 1) % n;
	plan.read.mix = wrapped - std::floor(wrapped);
	mark.readFrame = newRead;

	if (wish.cue) return plan;

	int startIdx;
	int steps;
	if (mark.lastWriteIdx < 0) {
		startIdx = idx0;
		steps = 1;
	} else {
		startIdx = (mark.lastWriteIdx + 1) % n;
		const int diff = (idx0 - mark.lastWriteIdx + n) % n;
		if (diff == 0)
			steps = wraps > 0 ? n : 0;
		else
			steps = diff;
	}

	const SlotAction action =
		wish.bake ? SlotAction::Bake : wish.overdub ? SlotAction::Overdub : SlotAction::Copy;
	const bool painting = wish.overdub || wish.bake;
	BankWrite bank;
	for (int s = 0; s < steps; ++s) {
		const int slot = (startIdx + s) % n;
		if (!painting && mark.backWritten[slot]) continue;
		bank.fills.push_back(SlotFill{slot, action});
		mark.backWritten[slot] = 1;
	}
	mark.lastWriteIdx = idx0;

	if (wraps <= 0) {
		if (!bank.fills.empty()) plan.banks.push_back(bank);
		return plan;
	}
	bank.swap = true;
	plan.banks.push_back(bank);
	for (int extra = 1; extra < wraps; ++extra) plan.banks.push_back(BankWrite{.swap = true});
	mark.backWritten.assign(n, 0);
	return plan;
}
