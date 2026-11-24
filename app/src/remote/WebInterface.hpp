#pragma once

#include "remote/Inbound.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class FolderList;
class Instrument;
class ParamBus;
class UsageLog;
class Wire;

struct WebLive {
	const ParamBus& params;
	const FolderList& folders;
	float loop = 0;
	float envelope = 0;
	int stage = 0;
	bool gate = false;
	bool cue = false;
	bool erase = false;
	std::vector<std::pair<std::string, std::string>> thumbs;
	bool thumbsFresh = false;
	std::string loadFailure;
	bool loadSwapped = false;
	std::string exportFailure;
	bool enabled = false;
	bool tokenRequired = true;
	int silenceMs = 300;
};

class WebInterface {
public:
	explicit WebInterface(std::filesystem::path tokenFile);
	WebInterface(Wire& wire, std::filesystem::path usageFile);
	~WebInterface();
	WebInterface(const WebInterface&) = delete;
	WebInterface& operator=(const WebInterface&) = delete;

	struct Step {
		bool gate = false;
		bool cue = false;
		bool erase = false;
		std::string draw;
		bool hasDraw = false;
	};

	Step frame(Instrument& instrument, const WebLive& live);
	void setUsageFile(std::filesystem::path path);
	bool enabled() const;
	std::string url() const;
	int clientCount() const;

private:
	void carry(Instrument& instrument, const Inbound& msg);
	void publish(const WebLive& live);
	void noteUse(const Inbound& msg);

	std::unique_ptr<Wire> owned;
	Wire* wire = nullptr;
	std::filesystem::path usagePath;
	std::unique_ptr<UsageLog> usage;
	std::string sourceFailure;
	std::string exportSeen;
	std::string lastSent;
	std::vector<std::string> sentNames;
	std::vector<std::string> sentPaths;
};
