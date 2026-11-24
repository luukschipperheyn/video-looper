#include "looper/LoopPlan.hpp"

#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) ++failures;
}

static bool same(const std::vector<int>& values, int expected) {
	return std::all_of(values.begin(), values.end(), [expected](int v) { return v == expected; });
}

static const int kFrames = 120;
static const int kStale = 0;
static const int kLoop = 1;

struct LoopPictures {
	std::vector<int> front;
	std::vector<int> back;
	std::vector<int> wrote;
	std::vector<int> firstPass;
	int passes = 0;
	int badPasses = 0;

	static LoopPictures make(int frames) {
		LoopPictures pictures;
		pictures.front.assign(frames, kLoop);
		pictures.back.assign(frames, kStale);
		pictures.wrote.assign(frames, 0);
		return pictures;
	}

	void apply(const LoopPlan& plan, int overdub, int bake) {
		for (const BankWrite& bank : plan.banks) {
			for (const SlotFill& fill : bank.fills) {
				wrote[fill.index] += 1;
				if (fill.action == SlotAction::Copy)
					back[fill.index] = front[fill.index];
				else if (fill.action == SlotAction::Overdub)
					back[fill.index] = overdub;
				else
					back[fill.index] = bake;
			}
			if (!bank.swap) continue;
			if (passes == 0) firstPass = wrote;
			++passes;
			bool ok = true;
			for (int count : wrote) ok = ok && count == 1;
			if (!ok) ++badPasses;
			std::swap(front, back);
			std::fill(wrote.begin(), wrote.end(), 0);
		}
	}
};

static const float kDt = 1.f / 60.f;

static void playback(LoopMark& mark, LoopPictures& pictures, int count, float readFps) {
	const LoopWish play;
	for (int i = 0; i < count; ++i) pictures.apply(planLoop(mark, kFrames, kDt, readFps, play), 0, 0);
}

static std::vector<float> steady(int count) {
	return std::vector<float>(count, kDt);
}

static std::vector<float> jittered(int count) {
	std::mt19937 rng(1);
	std::uniform_real_distribution<float> spread(0.9f, 1.1f);
	std::vector<float> dts;
	dts.reserve(count);
	for (int i = 0; i < count; ++i) dts.push_back(kDt * spread(rng));
	return dts;
}

static std::vector<float> dropped(int count) {
	std::mt19937 rng(1);
	std::uniform_real_distribution<float> unit(0.f, 1.f);
	std::vector<float> dts;
	dts.reserve(count);
	for (int i = 0; i < count; ++i) dts.push_back(unit(rng) < 0.02f ? 1.f / 30.f : kDt);
	return dts;
}

static void everySlotOnce(const char* what, float readFps, const std::vector<float>& dts) {
	LoopMark mark;
	LoopPictures pictures = LoopPictures::make(kFrames);
	const LoopWish play;
	for (float dt : dts) pictures.apply(planLoop(mark, kFrames, dt, readFps, play), 0, 0);
	check(pictures.passes > 0 && pictures.badPasses == 0, what);
}

int main() {
	{
		LoopMark mark;
		LoopPictures pictures = LoopPictures::make(kFrames);
		playback(mark, pictures, 60, 120.f);
		const bool wroteLoopPoint = pictures.firstPass.size() == static_cast<size_t>(kFrames) && pictures.firstPass[119] == 1;
		check(pictures.passes == 1 && wroteLoopPoint && pictures.badPasses == 0,
		      "1. a 120 fps read writes the slot at the loop point once");
	}

	const int minutes = 60 * 600;
	for (float readFps : {30.f, 60.f, 90.f, 120.f, 240.f}) {
		char steadyName[80];
		char jitterName[80];
		char dropName[80];
		std::snprintf(steadyName, sizeof steadyName, "2. %.0f fps steady writes every slot once per pass", readFps);
		std::snprintf(jitterName, sizeof jitterName, "2. %.0f fps with jitter writes every slot once per pass", readFps);
		std::snprintf(dropName, sizeof dropName, "2. %.0f fps with dropped frames writes every slot once per pass", readFps);
		everySlotOnce(steadyName, readFps, steady(minutes));
		everySlotOnce(jitterName, readFps, jittered(minutes));
		everySlotOnce(dropName, readFps, dropped(minutes));
	}

	{
		LoopMark mark;
		LoopPictures pictures = LoopPictures::make(kFrames);
		playback(mark, pictures, 100, 60.f);
		check(pictures.passes == 0, "3. playback before the loop point does not swap");
		const std::vector<int> before = pictures.front;
		LoopWish cue;
		cue.cue = true;
		bool cueWrote = false;
		for (int i = 0; i < 30; ++i) {
			LoopPlan plan = planLoop(mark, kFrames, kDt, 60.f, cue);
			if (!plan.banks.empty()) cueWrote = true;
			pictures.apply(plan, 0, 0);
		}
		check(!cueWrote && pictures.passes == 0 && pictures.front == before,
		      "3. Cue across the loop point leaves the Loop unchanged");
		playback(mark, pictures, 1, 60.f);
		check(pictures.passes == 1 && pictures.badPasses == 0 && pictures.front == before,
		      "3. after Cue the unfinished pass copies forward and the banks stay coherent");
	}

	{
		LoopMark mark;
		LoopPictures pictures = LoopPictures::make(kFrames);
		LoopWish dub;
		dub.overdub = true;
		const int stamp = 4;
		for (int i = 0; i < 120; ++i) pictures.apply(planLoop(mark, kFrames, kDt, 60.f, dub), stamp, 0);
		check(pictures.passes == 1 && pictures.badPasses == 0 && same(pictures.front, stamp),
		      "4. an Overdub fills every slot once and that is the Loop");
		const int passes = pictures.passes;
		playback(mark, pictures, 120, 60.f);
		check(pictures.passes == passes + 1 && pictures.badPasses == 0 && same(pictures.front, stamp),
		      "4. playback after an Overdub copies it forward");
	}

	{
		LoopMark mark;
		LoopPictures pictures = LoopPictures::make(kFrames);
		LoopWish baked;
		baked.overdub = true;
		baked.bake = true;
		const int stamp = 8;
		for (int i = 0; i < 120; ++i) pictures.apply(planLoop(mark, kFrames, kDt, 60.f, baked), 4, stamp);
		check(pictures.passes == 1 && pictures.badPasses == 0 && same(pictures.front, stamp),
		      "5. Bake FX replaces every slot once and wins over an Overdub");
	}

	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
