#include "remote/RemoteApply.hpp"

#include "control/ControlTable.hpp"
#include "control/Instrument.hpp"
#include "ui/ParamBus.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {

std::string jsonString(const std::string& s) {
	std::string out = "\"";
	for (char ch : s) {
		unsigned char c = static_cast<unsigned char>(ch);
		if (c == '"' || c == '\\') {
			out += '\\';
			out += ch;
		} else if (c < 0x20) {
			char buf[8];
			std::snprintf(buf, sizeof buf, "\\u%04x", c);
			out += buf;
		} else {
			out += ch;
		}
	}
	return out + "\"";
}

std::string jsonNumber(float v) {
	char buf[32];
	auto res = std::to_chars(buf, buf + sizeof buf, v);
	return std::string(buf, res.ptr);
}

bool parseNumber(const std::string& raw, double& out) {
	if (raw.empty() || !(raw[0] == '-' || std::isdigit(static_cast<unsigned char>(raw[0])))) return false;
	char* end = nullptr;
	out = std::strtod(raw.c_str(), &end);
	return end == raw.c_str() + raw.size() && std::isfinite(out);
}

std::string rejectOf(const Outcome& outcome) {
	return outcome.ok ? std::string() : rejectFrame(outcome.topic, outcome.reason);
}

std::string applySet(Instrument& instrument, const RemoteSet& set) {
	const Control* control = findControl(set.name);
	if (!control || control->web != Web::Write) return rejectFrame(set.name, "read-only");
	if (control->type == ControlType::Bool) {
		if (set.raw != "true" && set.raw != "false") return rejectFrame(set.name, "bad value");
		return rejectOf(instrument.setSwitch(set.name, set.raw == "true"));
	}
	double n;
	if (!parseNumber(set.raw, n)) return rejectFrame(set.name, "bad value");
	return rejectOf(instrument.setNumber(set.name, n));
}

} // namespace

std::string applyRemote(Instrument& instrument, const RemoteSet& set) {
	switch (set.kind) {
	case RemoteKind::Set:
		return applySet(instrument, set);
	case RemoteKind::Panic:
		instrument.panic();
		break;
	case RemoteKind::Prev:
		instrument.prevVideo();
		break;
	case RemoteKind::Next:
		instrument.nextVideo();
		break;
	case RemoteKind::Webcam:
		instrument.chooseWebcam();
		break;
	case RemoteKind::Load:
		return rejectOf(instrument.loadVideoNamed(set.name));
	case RemoteKind::Dub:
		return rejectOf(instrument.dubVideoNamed(set.name));
	case RemoteKind::Page:
		instrument.primePage(set.names);
		break;
	case RemoteKind::Folder:
		return rejectOf(instrument.chooseFolder(set.name));
	}
	return {};
}

std::string remoteSnapshot(const ParamBus& params, const RemoteLive& live, const std::vector<std::string>& videos,
                           const std::vector<std::string>& folders, bool catalog, const std::string& sourceError) {
	std::string out = "{\"t\":\"snapshot\"";
	for (const Control& c : kControls) {
		if (c.web == Web::Hidden) continue;
		out += ",\"";
		out += c.name;
		out += "\":";
		switch (c.type) {
		case ControlType::Bool:
			out += params.getBool(c.name) ? "true" : "false";
			break;
		case ControlType::Float:
			out += jsonNumber(params.getFloat(c.name));
			break;
		case ControlType::Int:
			out += std::to_string(params.getInt(c.name));
			break;
		case ControlType::String:
			out += jsonString(params.getString(c.name));
			break;
		}
	}
	std::string video;
	if (params.getInt("source_kind") == 1) video = std::filesystem::path(params.getString("file_path")).filename();
	out += ",\"loop\":" + jsonNumber(live.loop) + ",\"envelope\":" + jsonNumber(live.envelope) +
	       ",\"stage\":" + std::to_string(live.stage) + ",\"gate\":" + (live.gate ? "true" : "false") +
	       ",\"cue\":" + (live.cue ? "true" : "false") + ",\"erase\":" + (live.erase ? "true" : "false") +
	       ",\"video\":" + jsonString(video) + ",\"source_error\":" + jsonString(sourceError);
	if (!catalog) return out + "}";
	out += ",\"videos\":[";
	for (size_t i = 0; i < videos.size(); ++i)
		out += (i ? "," : "") + jsonString(videos[i]);
	out += "],\"folders\":[";
	for (size_t i = 0; i < folders.size(); ++i)
		out += (i ? "," : "") + jsonString(folders[i]);
	return out + "]}";
}

std::string rejectFrame(const std::string& name, const std::string& reason) {
	return "{\"t\":\"reject\",\"name\":" + jsonString(name) + ",\"reason\":" + jsonString(reason) + "}";
}
