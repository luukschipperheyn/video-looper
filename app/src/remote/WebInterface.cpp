#include "remote/WebInterface.hpp"

#include "control/ControlTable.hpp"
#include "control/Instrument.hpp"
#include "io/FolderList.hpp"
#include "io/VideoSource.hpp"
#include "remote/RemoteApply.hpp"
#include "remote/RemoteSession.hpp"
#include "remote/UsageLog.hpp"
#include "ui/ParamBus.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace {

bool parseNumber(const std::string& raw, double& out) {
	if (raw.empty() || !(raw[0] == '-' || std::isdigit(static_cast<unsigned char>(raw[0])))) return false;
	char* end = nullptr;
	out = std::strtod(raw.c_str(), &end);
	return end == raw.c_str() + raw.size() && std::isfinite(out);
}

std::string applySet(Instrument& instrument, const Inbound& msg) {
	const Control* control = findControl(msg.name);
	if (!control || control->web != Web::Write) return rejectFrame(msg.name, "read-only");
	if (control->type == ControlType::Bool) {
		if (msg.raw != "true" && msg.raw != "false") return rejectFrame(msg.name, "bad value");
		Outcome outcome = instrument.setSwitch(msg.name, msg.raw == "true");
		return outcome.ok ? std::string() : rejectFrame(outcome.topic, outcome.reason);
	}
	double n;
	if (!parseNumber(msg.raw, n)) return rejectFrame(msg.name, "bad value");
	Outcome outcome = instrument.setNumber(msg.name, n);
	return outcome.ok ? std::string() : rejectFrame(outcome.topic, outcome.reason);
}

bool keepsLoadFailure(const ParamBus& params) {
	int kind = params.getInt("source_kind");
	return kind == int(SourceKind::File) || kind == int(SourceKind::Webcam);
}

std::vector<std::string> videoNames(const FolderList& folders) {
	std::vector<std::string> names;
	const PreparedFolder* folder = folders.current();
	if (!folder) return names;
	names.reserve(folder->videos.size());
	for (const std::string& path : folder->videos)
		names.push_back(std::filesystem::path(path).filename().string());
	return names;
}

constexpr int kUsageNameMax = 64;
constexpr int kUsageValueMax = 512;

bool plainToken(const std::string& raw, std::string& plain, int maxLen) {
	if (raw.size() < 3 || raw[0] != '"' || raw.find('\\') != std::string::npos) return false;
	plain = raw.substr(1, raw.size() - 2);
	return int(plain.size()) <= maxLen && plain.find_first_of("\r\n") == std::string::npos;
}

bool usageEvent(const std::string& event) {
	return event == "set" || event == "hold" || event == "release" || event == "open" || event == "close" ||
	       event == "chain" || event == "tap" || event == "act";
}

bool usageValue(const std::string& raw) {
	if (raw.empty() || raw == "true" || raw == "false") return true;
	if (raw[0] == '"') {
		std::string plain;
		return plainToken(raw, plain, kUsageValueMax);
	}
	if (raw[0] != '-' && !std::isdigit(static_cast<unsigned char>(raw[0]))) return false;
	char* end = nullptr;
	std::strtod(raw.c_str(), &end);
	return end == raw.c_str() + raw.size();
}

}

WebInterface::WebInterface(std::filesystem::path tokenFile)
	: owned(std::make_unique<RemoteSession>(std::move(tokenFile))), wire(owned.get()) {}

WebInterface::WebInterface(Wire& adopted, std::filesystem::path usageFile)
	: wire(&adopted), usagePath(std::move(usageFile)) {}

WebInterface::~WebInterface() = default;

void WebInterface::setUsageFile(std::filesystem::path path) {
	usagePath = std::move(path);
}

bool WebInterface::enabled() const {
	return wire->enabled();
}

std::string WebInterface::url() const {
	return wire->url();
}

int WebInterface::clientCount() const {
	return wire->clientCount();
}

WebInterface::Step WebInterface::frame(Instrument& instrument, const WebLive& live) {
	wire->setTokenRequired(live.tokenRequired);
	wire->setSilenceWindow(live.silenceMs);
	wire->setEnabled(live.enabled);
	if (live.enabled && !wire->enabled()) instrument.setSwitch("ipad_page", false);
	if (live.thumbsFresh) wire->setThumbs(live.thumbs);
	if (wire->enabled()) {
		bool keepFailure = keepsLoadFailure(live.params);
		for (const Inbound& msg : wire->takeMessages())
			carry(instrument, msg);
		if (!live.loadFailure.empty()) {
			sourceFailure = live.loadFailure;
			wire->send(rejectFrame("source", live.loadFailure));
		} else if (live.loadSwapped || !keepFailure) {
			sourceFailure.clear();
		}
		if (live.exportFailure != exportSeen) {
			exportSeen = live.exportFailure;
			if (!exportSeen.empty()) wire->send(rejectFrame("record_enabled", exportSeen));
		}
		publish(live);
	}
	PadHolds pads = wire->pads();
	Step step;
	step.gate = pads.gate;
	step.cue = pads.cue;
	step.erase = pads.erase;
	step.hasDraw = wire->takeDraw(step.draw);
	return step;
}

void WebInterface::carry(Instrument& instrument, const Inbound& msg) {
	if (msg.type == "set") {
		std::string reject = applySet(instrument, msg);
		if (!reject.empty()) wire->send(reject);
		return;
	}
	if (msg.type == "panic") {
		wire->releaseHolds();
		instrument.panic();
		return;
	}
	if (msg.type == "prev") {
		instrument.prevVideo();
		return;
	}
	if (msg.type == "next") {
		instrument.nextVideo();
		return;
	}
	if (msg.type == "load") {
		Outcome outcome = instrument.loadVideoNamed(msg.name);
		if (!outcome.ok) wire->send(rejectFrame(outcome.topic, outcome.reason));
		return;
	}
	if (msg.type == "dub") {
		Outcome outcome = instrument.dubVideoNamed(msg.name);
		if (!outcome.ok) wire->send(rejectFrame(outcome.topic, outcome.reason));
		return;
	}
	if (msg.type == "page") {
		instrument.primePage(msg.names);
		return;
	}
	if (msg.type == "folder") {
		Outcome outcome = instrument.chooseFolder(msg.name);
		if (!outcome.ok) wire->send(rejectFrame(outcome.topic, outcome.reason));
		return;
	}
	if (msg.type == "use") noteUse(msg);
}

void WebInterface::publish(const WebLive& live) {
	std::vector<std::string> names = videoNames(live.folders);
	std::vector<std::string> paths = live.folders.paths();
	bool catalogChanged = names != sentNames || paths != sentPaths;
	RemoteLive picture{live.loop, live.envelope, live.stage, live.gate, live.cue, live.erase};
	std::string full =
		remoteSnapshot(live.params, picture, names, paths, true, sourceFailure, exportSeen);
	wire->setSnapshot(full);
	std::string body = catalogChanged
	                       ? full
	                       : remoteSnapshot(live.params, picture, {}, {}, false, sourceFailure, exportSeen);
	if (body != lastSent) {
		lastSent = body;
		wire->send(body);
	}
	if (catalogChanged) {
		sentNames = std::move(names);
		sentPaths = std::move(paths);
	}
}

void WebInterface::noteUse(const Inbound& msg) {
	if (usagePath.empty()) return;
	if (!usageEvent(msg.event) || int(msg.event.size()) > kUsageNameMax) return;
	if (msg.name.empty() || int(msg.name.size()) > kUsageNameMax) return;
	if (!usageValue(msg.raw)) return;
	if (!usage) usage = std::make_unique<UsageLog>(usagePath);
	usage->write(msg.event, msg.name, msg.raw);
}
