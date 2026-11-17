#pragma once

#include <string>

class ParamBus;

// One set from the iPad. raw is the JSON value token as sent: true, false, 140, or "x".
struct RemoteSet {
	std::string name;
	std::string raw;
};

struct ApplyResult {
	bool reject;
	std::string reason;
	bool tempoChanged;
	float bpm;
};

// Clamps and writes a writable param. Never calls Link; the caller hands bpm to Link when tempoChanged.
ApplyResult applyRemoteSet(ParamBus& params, const RemoteSet& set);
std::string remoteSnapshot(const ParamBus& params);
std::string rejectFrame(const std::string& name, const std::string& reason);
