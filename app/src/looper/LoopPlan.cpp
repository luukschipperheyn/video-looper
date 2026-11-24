#include "looper/LoopPlan.hpp"

#include <cmath>
#include <utility>

namespace {

struct Draft {
	LoopPlan& plan;
	BankWrite bank;

	void add(int from, int to, SlotAction action) {
		for (int slot = from; slot <= to; ++slot) bank.fills.push_back(SlotFill{slot, action});
	}

	void flush(bool swap) {
		if (bank.fills.empty() && !swap) return;
		bank.swap = swap;
		plan.banks.push_back(std::move(bank));
		bank = {};
	}
};

SlotAction actionFor(const LoopWish& wish) {
	if (wish.bake) return SlotAction::Bake;
	if (wish.overdub) return SlotAction::Overdub;
	return SlotAction::Copy;
}

}

LoopPlan planLoop(LoopMark& mark, int frames, float dtSeconds, float readFps, const LoopWish& wish) {
	LoopPlan plan;
	const int n = frames;
	if (n <= 0) return plan;

	float wrapped = mark.readFrame;
	if (wrapped < 0.f) wrapped += static_cast<float>(n);
	const int idx0 = static_cast<int>(std::floor(wrapped)) % n;
	plan.read.idx0 = idx0;
	plan.read.idx1 = (idx0 + 1) % n;
	plan.read.mix = wrapped - std::floor(wrapped);

	float end = mark.readFrame + readFps * dtSeconds;
	int crossings = 0;
	if (end >= static_cast<float>(n)) {
		crossings = static_cast<int>(std::floor(end / static_cast<float>(n)));
		end = std::fmod(end, static_cast<float>(n));
	}
	mark.readFrame = end;

	if (wish.cue) {
		mark.owedSwaps += crossings;
		return plan;
	}

	Draft draft{plan, {}};
	const SlotAction action = actionFor(wish);

	if (mark.owedSwaps > 0) {
		if (mark.nextSlot < n) draft.add(mark.nextSlot, n - 1, SlotAction::Copy);
		draft.flush(true);
		for (int extra = 1; extra < mark.owedSwaps; ++extra) {
			draft.add(0, n - 1, SlotAction::Copy);
			draft.flush(true);
		}
		mark.owedSwaps = 0;
		mark.nextSlot = 0;
		if (plan.read.idx0 > 0) {
			draft.add(0, plan.read.idx0 - 1, SlotAction::Copy);
			mark.nextSlot = plan.read.idx0;
		}
	}

	if (mark.nextSlot <= plan.read.idx0) {
		draft.add(mark.nextSlot, plan.read.idx0, action);
		mark.nextSlot = plan.read.idx0 + 1;
	}

	if (crossings > 0) {
		if (mark.nextSlot < n) draft.add(mark.nextSlot, n - 1, action);
		draft.flush(true);
		for (int extra = 1; extra < crossings; ++extra) {
			draft.add(0, n - 1, action);
			draft.flush(true);
		}
		mark.nextSlot = 0;
	} else {
		draft.flush(false);
	}
	return plan;
}
