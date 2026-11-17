#include "util/ContainRect.hpp"

#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) ++failures;
}

int main() {
	ContainRect same = containRect(1920, 1080, 1280, 720);
	check(same.x == 0 && same.y == 0 && same.w == 1280 && same.h == 720, "matching 16:9 fills both");

	ContainRect wider = containRect(1920, 1080, 1280, 960);
	check(wider.w == 1280 && wider.x == 0, "wider video fills width");
	check(wider.h == 720 && wider.y == 120, "wider video letterboxes");

	ContainRect taller = containRect(640, 480, 1280, 720);
	check(taller.h == 720 && taller.y == 0, "taller video fills height");
	check(taller.w == 960 && taller.x == 160, "taller video pillarboxes");

	ContainRect square = containRect(1000, 1000, 1920, 1080);
	check(square.h == 1080 && square.y == 0, "square fills height of a wide frame");
	check(square.w == 1080 && square.x == 420, "square is centered horizontally");

	ContainRect empty = containRect(0, 1080, 1280, 720);
	check(empty.w == 0 && empty.h == 0, "missing source yields an empty rect");

	return failures == 0 ? 0 : 1;
}
