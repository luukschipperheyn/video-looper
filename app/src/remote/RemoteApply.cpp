#include "remote/RemoteApply.hpp"

#include "remote/ControlSchema.hpp"
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

ApplyResult rejected(const char* reason) { return {true, reason, false, 0.0f}; }

} // namespace

ApplyResult applyRemoteSet(ParamBus& params, const RemoteSet& set) {
	const Control* control = findControl(set.name);
	if (!control || !control->writable) return rejected("read-only");
	if (control->type == ControlType::Bool) {
		if (set.raw != "true" && set.raw != "false") return rejected("bad value");
		bool on = set.raw == "true";
		if (set.name == "record_enabled" && on && params.getString("record_path").empty())
			return rejected("no export path");
		params.setBool(set.name, on);
		return {};
	}
	double n;
	if (!parseNumber(set.raw, n)) return rejected("bad value");
	n = std::clamp(n, double(control->min), double(control->max));
	if (control->type == ControlType::Int) {
		int v = int(std::lround(n));
		if (set.name == "source_kind" && v == 3) return {false, {}, false, 0.0f, true};
		params.setInt(set.name, v);
		return {};
	}
	float v = float(n);
	bool tempoChanged = set.name == "bpm" && v != params.getFloat("bpm", 120.0f);
	params.setFloat(set.name, v);
	return {false, {}, tempoChanged, v};
}

GateState combineHolds(GateInputs in) {
	return {in.latch || in.space || in.pad, in.cueLatch || in.cueKey || in.cuePad};
}

std::string remoteSnapshot(const ParamBus& params, const RemoteLive& live, const std::vector<std::string>& videos) {
	std::string out = "{\"t\":\"snapshot\"";
	for (const Control& c : kControls) {
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
	       ",\"cue\":" + (live.cue ? "true" : "false") + ",\"video\":" + jsonString(video) + ",\"videos\":[";
	for (size_t i = 0; i < videos.size(); ++i) out += (i ? "," : "") + jsonString(videos[i]);
	return out + "]}";
}

std::string rejectFrame(const std::string& name, const std::string& reason) {
	return "{\"t\":\"reject\",\"name\":" + jsonString(name) + ",\"reason\":" + jsonString(reason) + "}";
}
