#include "gl/PictureChoice.hpp"

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

static const char* pictureName(Picture picture) {
	switch (picture) {
	case Picture::Source:
		return "Source";
	case Picture::Audition:
		return "Audition";
	case Picture::Program:
		return "Program";
	}
	return "unknown";
}

static void expectShown(const char* moment, const PictureChoice& choice, Picture want) {
	char line[128];
	std::snprintf(line, sizeof line, "%s window asks for the %s", moment, pictureName(want));
	check(choice.shown == want, line);
}

static void expectPreview(const char* moment, const PictureChoice& choice, float writeOpacity, float recordOpacity) {
	char line[160];
	if (choice.previewed == Picture::Audition && near(choice.previewOpacity, writeOpacity)) {
		std::snprintf(line, sizeof line, "%s preview asks for the Audition at the envelope", moment);
		check(true, line);
		return;
	}
	if (choice.previewed == Picture::Audition && near(choice.previewOpacity, recordOpacity)) {
		std::snprintf(line, sizeof line, "%s preview asks for the Audition at full record opacity", moment);
		check(false, line);
		return;
	}
	std::snprintf(line, sizeof line, "%s preview asks for the %s at %g", moment, pictureName(choice.previewed),
	              choice.previewOpacity);
	check(false, line);
}

static void expectExport(const char* moment, const PictureChoice& choice) {
	char line[128];
	if (choice.exported == Picture::Program) {
		std::snprintf(line, sizeof line, "%s Export asks for the Program", moment);
		check(true, line);
		return;
	}
	std::snprintf(line, sizeof line, "%s Export asks for the %s", moment, pictureName(choice.exported));
	check(false, line);
}

static void frame(const char* moment, PictureMoment which, float writeOpacity, Picture shown) {
	const float recordOpacity = 1.0f;
	PictureChoice choice = choosePictures(which, writeOpacity, recordOpacity);
	expectShown(moment, choice, shown);
	expectPreview(moment, choice, writeOpacity, recordOpacity);
	expectExport(moment, choice);
}

int main() {
	frame("idle", PictureMoment::Idle, 0.0f, Picture::Program);
	frame("Cue", PictureMoment::Cue, 0.25f, Picture::Audition);
	frame("Overdub", PictureMoment::Overdub, 0.25f, Picture::Audition);
	frame("Bake FX", PictureMoment::Bake, 0.25f, Picture::Audition);
	return failures == 0 ? 0 : 1;
}
