#pragma once

#include <string>
#include <vector>

class ParamBus;

enum class RemoteKind { Set, Panic, Prev, Next, Webcam, Load, Folder };

// One message from the iPad for the main thread. For a Set, raw is the JSON value token as sent: true, false,
// 140, or "x". A Load carries the Video's filename as name, a Folder the Videos folder's full path. The other
// kinds carry no name or value.
struct RemoteSet {
	std::string name;
	std::string raw;
	RemoteKind kind = RemoteKind::Set;
};

struct ApplyResult {
	bool reject;
	std::string reason;
	bool tempoChanged;
	float bpm;
	bool startWebcam = false;
};

// This frame's loop position as a fraction of the Loop, the envelope, and the combined holds.
struct RemoteLive {
	float loop = 0;
	float envelope = 0;
	int stage = 0;
	bool gate = false;
	bool cue = false;
};

struct GateInputs {
	bool latch;
	bool space;
	bool pad;
	bool cueLatch;
	bool cueKey;
	bool cuePad;
};

struct GateState {
	bool gate;
	bool cue;
};

// Clamps and writes a writable param. Never calls Link; the caller hands bpm to Link when tempoChanged.
// source_kind 3 (webcam) is not written; startWebcam asks the caller to load the webcam instead.
ApplyResult applyRemoteSet(ParamBus& params, const RemoteSet& set);
GateState combineHolds(GateInputs in);
// videos are the filenames of the Videos in the current Videos folder; folders are the Folder list's full paths.
// catalog false omits those lists, for the per-frame update. A new client still gets them from the stored snapshot.
std::string remoteSnapshot(const ParamBus& params, const RemoteLive& live = {},
                           const std::vector<std::string>& videos = {}, const std::vector<std::string>& folders = {},
                           bool catalog = true, const std::string& sourceError = {});
std::string rejectFrame(const std::string& name, const std::string& reason);
