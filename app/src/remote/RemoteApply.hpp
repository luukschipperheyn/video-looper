#pragma once

#include <string>

class ParamBus;

enum class RemoteKind { Set, Panic };

// One message from the iPad for the main thread. For a Set, raw is the JSON value token as sent: true, false,
// 140, or "x". A Panic carries no name or value.
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
ApplyResult applyRemoteSet(ParamBus& params, const RemoteSet& set);
GateState combineHolds(GateInputs in);
std::string remoteSnapshot(const ParamBus& params, const RemoteLive& live = {});
std::string rejectFrame(const std::string& name, const std::string& reason);
