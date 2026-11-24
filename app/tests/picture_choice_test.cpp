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

static void expectPreview(const char* moment, const PictureChoice& choice, float wantOpacity, const char* how) {
	char line[160];
	std::snprintf(line, sizeof line, "%s preview asks for the Audition at %s", moment, how);
	check(choice.previewed == Picture::Audition && near(choice.previewOpacity, wantOpacity), line);
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

static void frame(const char* moment, PictureMoment which, float writeOpacity, float recordOpacity, Picture shown,
                  float previewOpacity, const char* how) {
	PictureChoice choice = choosePictures(which, writeOpacity, recordOpacity);
	expectShown(moment, choice, shown);
	expectPreview(moment, choice, previewOpacity, how);
	expectExport(moment, choice);
}

int main() {
	frame("idle", PictureMoment::Idle, 0.0f, 0.4f, Picture::Program, 0.4f, "the record opacity");
	frame("Cue", PictureMoment::Cue, 0.25f, 0.4f, Picture::Audition, 0.25f, "the envelope");
	frame("Overdub", PictureMoment::Overdub, 0.25f, 0.4f, Picture::Audition, 0.25f, "the envelope");
	frame("Bake FX", PictureMoment::Bake, 0.25f, 0.4f, Picture::Audition, 0.25f, "the envelope");
	return failures == 0 ? 0 : 1;
}
