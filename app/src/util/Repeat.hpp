#pragma once

#include <algorithm>
#include <cmath>

// Repeat keeps the image and stamps extra copies along one step.
// Grid does the same on that step and on the perpendicular step.
// Distance is a fraction of the frame height. Turns run 0..1: 0 points right, and increasing turns point upward.
inline constexpr int kRepeatFxMode = 5;
inline constexpr int kGridFxMode = 6;
inline constexpr int kRepeatMaxCopies = 16;
inline constexpr int kGridMaxCopies = 6;

struct RepeatStep {
	float x;
	float y;
};

inline int repeatCopies(int count) {
	return std::clamp(count, 0, kRepeatMaxCopies);
}

inline int gridCopies(int count) {
	return std::clamp(count, 0, kGridMaxCopies);
}

inline bool fxStampsCopies(int mode) {
	return mode == kRepeatFxMode || mode == kGridFxMode;
}

// UV offset from one copy to the next. The original stays put; copy i sits i steps along this vector.
inline RepeatStep repeatStep(float distance, float turns, float frameW, float frameH) {
	float w = frameW > 0.0f ? frameW : 1.0f;
	float h = frameH > 0.0f ? frameH : 1.0f;
	float angle = turns * 6.28318530718f;
	float c = std::cos(angle);
	float s = std::sin(angle);
	return {c * distance * (h / w), s * distance};
}
