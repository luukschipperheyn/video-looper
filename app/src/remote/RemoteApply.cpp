#include "remote/RemoteApply.hpp"

#include "control/ControlTable.hpp"
#include "ui/ParamBus.hpp"

#include <charconv>
#include <cstdio>
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

} // namespace

std::string remoteSnapshot(const ParamBus& params, const RemoteLive& live, const std::vector<std::string>& videos,
                           const std::vector<std::string>& folders, bool catalog, const std::string& sourceError,
                           const std::string& exportError) {
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
	       ",\"video\":" + jsonString(video) + ",\"source_error\":" + jsonString(sourceError) +
	       ",\"export_error\":" + jsonString(exportError);
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
