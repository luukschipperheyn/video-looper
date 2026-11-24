#pragma once

#include <vector>

enum class SlotAction { Copy, Overdub, Bake };

struct SlotFill {
	int index = 0;
	SlotAction action = SlotAction::Copy;
};

struct BankWrite {
	std::vector<SlotFill> fills;
	bool swap = false;
};

struct ReadHead {
	int idx0 = 0;
	int idx1 = 0;
	float mix = 0.f;
};

struct LoopWish {
	bool cue = false;
	bool overdub = false;
	bool bake = false;
};

struct LoopMark {
	float readFrame = 0.f;
	int lastWriteIdx = -1;
	std::vector<char> backWritten;
};

struct LoopPlan {
	ReadHead read;
	std::vector<BankWrite> banks;
};

LoopPlan planLoop(LoopMark& mark, int frames, float dtSeconds, float readFps, const LoopWish& wish);
