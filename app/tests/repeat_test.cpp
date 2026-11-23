#include "util/Repeat.hpp"

#include <cmath>
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) ++failures;
}

static bool near(float a, float b) {
	return std::fabs(a - b) < 1e-4f;
}

int main() {
	check(repeatCopies(-3) == 0 && repeatCopies(0) == 0 && repeatCopies(4) == 4 && repeatCopies(40) == 16,
	      "the copy count stays inside 0..16");
	check(gridCopies(-1) == 0 && gridCopies(4) == 4 && gridCopies(40) == 6, "a grid stays inside 0..6 copies per axis");
	check(fxStampsCopies(kRepeatFxMode) && fxStampsCopies(kGridFxMode) && !fxStampsCopies(0),
	      "Repeat and Grid are the stamp effects");

	RepeatStep right = repeatStep(1.0f, 0.0f, 1920.0f, 1080.0f);
	check(near(right.x, 1080.0f / 1920.0f) && near(right.y, 0.0f),
	      "a full frame-height to the right is that fraction of the width");

	RepeatStep up = repeatStep(0.25f, 0.25f, 100.0f, 200.0f);
	check(near(up.x, 0.0f) && near(up.y, 0.25f), "a quarter turn points up by the distance");

	RepeatStep left = repeatStep(0.5f, 0.5f, 200.0f, 100.0f);
	check(near(left.x, -0.5f * (100.0f / 200.0f)) && near(left.y, 0.0f), "half a turn points left");

	RepeatStep stacked = repeatStep(0.0f, 0.3f, 1280.0f, 720.0f);
	check(near(stacked.x, 0.0f) && near(stacked.y, 0.0f), "zero distance stacks the copies");

	RepeatStep safe = repeatStep(0.2f, 0.0f, 0.0f, 0.0f);
	check(near(safe.x, 0.2f) && near(safe.y, 0.0f), "a missing frame size still yields a step");

	RepeatStep axis = repeatStep(0.5f, 0.2f, 4.0f, 2.0f);
	RepeatStep square = repeatStep(0.5f, 0.45f, 4.0f, 2.0f);
	float px = axis.x * 4.0f;
	float py = axis.y * 2.0f;
	check(near(square.x * 4.0f, -py) && near(square.y * 2.0f, px),
	      "a quarter turn stands square to the first axis");

	return failures == 0 ? 0 : 1;
}
