#pragma once

#include <string>
#include <vector>

class ParamBus;

// This frame's loop position as a fraction of the Loop, the envelope, and the combined holds.
struct RemoteLive {
	float loop = 0;
	float envelope = 0;
	int stage = 0;
	bool gate = false;
	bool cue = false;
	bool erase = false;
};

// videos are the filenames of the Videos in the current Videos folder; folders are the Folder list's full paths.
// catalog false omits those lists, for the per-frame update. A new client still gets them from the stored snapshot.
std::string remoteSnapshot(const ParamBus& params, const RemoteLive& live = {},
                           const std::vector<std::string>& videos = {}, const std::vector<std::string>& folders = {},
                           bool catalog = true, const std::string& sourceError = {},
                           const std::string& exportError = {});
std::string rejectFrame(const std::string& name, const std::string& reason);
