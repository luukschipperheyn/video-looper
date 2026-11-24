#pragma once

#include <algorithm>
#include <string>
#include <vector>

// The part of a Video that plays, as fractions of its length.
struct LoopRange {
	float start;
	float end;
};

// A start and a length as the controls hold them, clamped to the Video and kept at least 0.001 long.
inline LoopRange loopRange(float start, float length) {
	start = std::clamp(start, 0.0f, 1.0f);
	length = std::clamp(length, 0.0f, 1.0f);
	return {start, std::clamp(start + length, start + 0.001f, 1.0f)};
}

// The Video delta steps away from current, wrapping. An unknown current steps from just outside the list.
std::string stepVideo(const std::vector<std::string>& files, const std::string& current, int delta);
// The path in files whose filename is name, or empty.
std::string videoByName(const std::vector<std::string>& files, const std::string& name);
// The paths in files whose filenames are names, in that order. Unknown names are left out.
std::vector<std::string> videosByName(const std::vector<std::string>& files, const std::vector<std::string>& names);

// A Commit ends once the Video is not looping and its reader has no further frame. A tick that only holds the
// current frame does not end it.
inline bool commitReachedEnd(bool frameAdvanced, bool looping, bool readerEnded) {
	return !frameAdvanced && !looping && readerEnded;
}
