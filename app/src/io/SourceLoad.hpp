#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct LoadRequest {
	uint64_t id = 0;
	bool webcam = false; // false means a Video path
	std::string path;
	int webcamIndex = 0;
	// Where a Video starts. The first decoded frame is inside this range.
	float loopStart = 0;
	float loopLength = 1;
};

// One Source load at a time, and the Source it last committed. resolve runs on the loading thread; everything
// else runs on the main thread.
class SourceLoad {
public:
	// Each request cancels the one in flight.
	LoadRequest requestVideo(std::string path, float loopStart = 0, float loopLength = 1);
	LoadRequest requestWebcam(int index);
	// Drops a load that has not swapped, including one that is already ready.
	void cancel();
	bool busy() const;
	uint64_t inflight() const; // 0 when idle

	// Ignored when id is not the current in-flight id.
	void resolve(uint64_t id, bool ok, std::string reason);

	// On success, swaps the committed source in one step and returns true once.
	bool pollSwap(bool& webcam, std::string& path, int& webcamIndex);
	// On failure, leaves the committed source and returns the reason once. Empty if none.
	std::string takeFailure();

	bool committedWebcam() const;
	std::string committedPath() const;
	int committedWebcamIndex() const;

private:
	enum class Phase { Idle, Loading, Ready, Failed };

	LoadRequest request(LoadRequest next);

	mutable std::mutex mutex;
	uint64_t lastId = 0;
	Phase phase = Phase::Idle;
	LoadRequest pending{0, false, {}, 0};
	std::string reason;
	bool webcam = false;
	std::string path;
	int webcamIndex = 0;
};

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

// True only on the frame a still Source (shader 0 or black 2) is chosen. Staying there must not cancel a Video load.
bool stillSourceCancelsLoad(int kind, int previousKind);
