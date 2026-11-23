#include "setup/SetupStore.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <utility>

namespace {

struct BoolKey {
	const char* key;
	bool PerformanceSetup::* member;
};
struct IntKey {
	const char* key;
	int PerformanceSetup::* member;
};
struct FloatKey {
	const char* key;
	float PerformanceSetup::* member;
};
struct StringKey {
	const char* key;
	std::string PerformanceSetup::* member;
};

const BoolKey kBools[] = {
	{"env_enabled", &PerformanceSetup::env_enabled},     {"fx_enabled", &PerformanceSetup::fx_enabled},
	{"fx_commit", &PerformanceSetup::fx_commit},         {"source_fx_enabled", &PerformanceSetup::source_fx_enabled},
	{"video_bars", &PerformanceSetup::video_bars},       {"video_aspect", &PerformanceSetup::video_aspect},
	{"file_loop", &PerformanceSetup::file_loop},         {"file_paused", &PerformanceSetup::file_paused},
	{"tempo_coupled", &PerformanceSetup::tempo_coupled}, {"ipad_page", &PerformanceSetup::ipad_page},
};
const IntKey kInts[] = {
	{"record_blend_mode", &PerformanceSetup::record_blend_mode},
	{"fx_mode", &PerformanceSetup::fx_mode},
	{"fx_count", &PerformanceSetup::fx_count},
	{"fx_repeat_blend", &PerformanceSetup::fx_repeat_blend},
	{"source_fx_mode", &PerformanceSetup::source_fx_mode},
	{"source_fx_count", &PerformanceSetup::source_fx_count},
	{"source_fx_repeat_blend", &PerformanceSetup::source_fx_repeat_blend},
	{"source_kind", &PerformanceSetup::source_kind},
	{"webcam_index", &PerformanceSetup::webcam_index},
	{"file_run_mode", &PerformanceSetup::file_run_mode},
};
const FloatKey kFloats[] = {
	{"record_blend_opacity", &PerformanceSetup::record_blend_opacity},
	{"gain_mix", &PerformanceSetup::gain_mix},
	{"env_attack", &PerformanceSetup::env_attack},
	{"env_decay", &PerformanceSetup::env_decay},
	{"env_sustain", &PerformanceSetup::env_sustain},
	{"env_release", &PerformanceSetup::env_release},
	{"fx_param1", &PerformanceSetup::fx_param1},
	{"fx_param2", &PerformanceSetup::fx_param2},
	{"fx_param3", &PerformanceSetup::fx_param3},
	{"source_fx_param1", &PerformanceSetup::source_fx_param1},
	{"source_fx_param2", &PerformanceSetup::source_fx_param2},
	{"source_fx_param3", &PerformanceSetup::source_fx_param3},
	{"video_tx", &PerformanceSetup::video_tx},
	{"video_ty", &PerformanceSetup::video_ty},
	{"video_sx", &PerformanceSetup::video_sx},
	{"video_sy", &PerformanceSetup::video_sy},
	{"video_rot", &PerformanceSetup::video_rot},
	{"file_loop_start", &PerformanceSetup::file_loop_start},
	{"file_loop_length", &PerformanceSetup::file_loop_length},
	{"bpm", &PerformanceSetup::bpm},
	{"read_speed_mult", &PerformanceSetup::read_speed_mult},
	{"record_fps", &PerformanceSetup::record_fps},
};
const StringKey kStrings[] = {
	{"file_path", &PerformanceSetup::file_path},
	{"videos_folder", &PerformanceSetup::videos_folder},
	{"record_path", &PerformanceSetup::record_path},
};

enum class Field { Unknown, Bool, Int, Float, String, Folders };

Field fieldOf(const std::string& key) {
	for (const BoolKey& f : kBools)
		if (key == f.key) return Field::Bool;
	for (const IntKey& f : kInts)
		if (key == f.key) return Field::Int;
	for (const FloatKey& f : kFloats)
		if (key == f.key) return Field::Float;
	for (const StringKey& f : kStrings)
		if (key == f.key) return Field::String;
	if (key == "folders") return Field::Folders;
	return Field::Unknown;
}

std::string jsonString(const std::string& s) {
	std::string out = "\"";
	for (unsigned char c : s) {
		if (c == '"' || c == '\\') {
			out += '\\';
			out += static_cast<char>(c);
		} else if (c < 0x20) {
			char buf[8];
			std::snprintf(buf, sizeof buf, "\\u%04x", c);
			out += buf;
		} else {
			out += static_cast<char>(c);
		}
	}
	return out + "\"";
}

std::string jsonFloat(float v) {
	char buf[64];
	auto res =
		std::to_chars(buf, buf + sizeof buf, v, std::chars_format::general, std::numeric_limits<float>::max_digits10);
	if (res.ec != std::errc()) return "0";
	return {buf, res.ptr};
}

std::string canonicalSetup(const PerformanceSetup& setup) {
	std::string out = "{";
	bool first = true;
	auto comma = [&] {
		if (!first) out += ',';
		first = false;
	};
	for (const BoolKey& f : kBools) {
		comma();
		out += jsonString(f.key);
		out += setup.*(f.member) ? ":true" : ":false";
	}
	for (const IntKey& f : kInts) {
		comma();
		out += jsonString(f.key);
		out += ':';
		out += std::to_string(setup.*(f.member));
	}
	for (const FloatKey& f : kFloats) {
		comma();
		out += jsonString(f.key);
		out += ':';
		out += jsonFloat(setup.*(f.member));
	}
	for (const StringKey& f : kStrings) {
		comma();
		out += jsonString(f.key);
		out += ':';
		out += jsonString(setup.*(f.member));
	}
	comma();
	out += "\"folders\":[";
	for (size_t i = 0; i < setup.folders.size(); ++i)
		out += (i ? "," : "") + jsonString(setup.folders[i]);
	out += "]}\n";
	return out;
}

bool writeText(const std::filesystem::path& path, const std::string& text) {
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	if (ec) return false;
	std::filesystem::path tmp = path;
	tmp += ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		out << text;
		if (!out) return false;
	}
	std::filesystem::rename(tmp, path, ec);
	if (ec) {
		std::filesystem::remove(tmp);
		return false;
	}
	return true;
}

class Parser {
public:
	explicit Parser(std::string_view text) : in(text) {}

	bool parse(PerformanceSetup& setup) {
		if (!eat('{')) return false;
		skip();
		if (peek('}')) {
			++i;
			skip();
			return i == in.size();
		}
		for (;;) {
			std::string key;
			if (!parseString(key) || !eat(':')) return false;
			switch (fieldOf(key)) {
			case Field::Bool: {
				bool value;
				if (!parseBool(value)) return false;
				for (const BoolKey& f : kBools)
					if (key == f.key) setup.*(f.member) = value;
				break;
			}
			case Field::Int:
			case Field::Float: {
				double value;
				if (!parseNumber(value)) return false;
				if (!assignNumber(setup, key, value)) return false;
				break;
			}
			case Field::String: {
				std::string value;
				if (!parseString(value)) return false;
				for (const StringKey& f : kStrings)
					if (key == f.key) setup.*(f.member) = value;
				break;
			}
			case Field::Folders:
				if (!parseStringArray(setup.folders)) return false;
				break;
			case Field::Unknown:
				if (!skipValue()) return false;
				break;
			}
			skip();
			if (peek(',')) {
				++i;
				continue;
			}
			break;
		}
		if (!eat('}')) return false;
		skip();
		return i == in.size();
	}

private:
	std::string_view in;
	size_t i = 0;

	void skip() {
		while (i < in.size() && std::isspace(static_cast<unsigned char>(in[i])))
			++i;
	}
	bool peek(char c) {
		skip();
		return i < in.size() && in[i] == c;
	}
	bool eat(char c) {
		if (!peek(c)) return false;
		++i;
		return true;
	}
	bool parseString(std::string& out) {
		if (!eat('"')) return false;
		out.clear();
		while (i < in.size()) {
			char c = in[i++];
			if (c == '"') return true;
			if (c != '\\') {
				if (static_cast<unsigned char>(c) < 0x20) return false;
				out += c;
				continue;
			}
			if (i >= in.size()) return false;
			char e = in[i++];
			if (e == 'u') {
				if (i + 4 > in.size()) return false;
				int code = 0;
				for (int k = 0; k < 4; ++k) {
					char h = in[i++];
					code <<= 4;
					if (h >= '0' && h <= '9')
						code += h - '0';
					else if (h >= 'a' && h <= 'f')
						code += h - 'a' + 10;
					else if (h >= 'A' && h <= 'F')
						code += h - 'A' + 10;
					else
						return false;
				}
				if (code < 0x80)
					out += static_cast<char>(code);
				else if (code < 0x800) {
					out += static_cast<char>(0xc0 | (code >> 6));
					out += static_cast<char>(0x80 | (code & 0x3f));
				} else {
					out += static_cast<char>(0xe0 | (code >> 12));
					out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
					out += static_cast<char>(0x80 | (code & 0x3f));
				}
			} else if (e == '"' || e == '\\' || e == '/')
				out += e;
			else if (e == 'n')
				out += '\n';
			else if (e == 't')
				out += '\t';
			else if (e == 'r')
				out += '\r';
			else
				return false;
		}
		return false;
	}
	bool parseNumber(double& out) {
		skip();
		if (i >= in.size()) return false;
		const char* begin = in.data() + i;
		char* end = nullptr;
		out = std::strtod(begin, &end);
		if (end == begin || !std::isfinite(out)) return false;
		i += static_cast<size_t>(end - begin);
		return true;
	}
	bool parseBool(bool& out) {
		skip();
		if (in.substr(i, 4) == "true") {
			i += 4;
			out = true;
			return true;
		}
		if (in.substr(i, 5) == "false") {
			i += 5;
			out = false;
			return true;
		}
		return false;
	}
	bool parseStringArray(std::vector<std::string>& out) {
		out.clear();
		if (!eat('[')) return false;
		if (peek(']')) {
			++i;
			return true;
		}
		for (;;) {
			std::string value;
			if (!parseString(value)) return false;
			out.push_back(std::move(value));
			if (peek(',')) {
				++i;
				continue;
			}
			break;
		}
		return eat(']');
	}
	bool skipValue() {
		skip();
		if (i >= in.size()) return false;
		char c = in[i];
		if (c == '"') {
			std::string ignored;
			return parseString(ignored);
		}
		if (c == 't' || c == 'f') {
			bool ignored;
			return parseBool(ignored);
		}
		if (c == 'n' && in.substr(i, 4) == "null") {
			i += 4;
			return true;
		}
		if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
			double ignored;
			return parseNumber(ignored);
		}
		if (c == '[') return skipList('[', ']');
		if (c == '{') return skipList('{', '}');
		return false;
	}
	bool skipList(char open, char close) {
		if (!eat(open)) return false;
		int depth = 1;
		bool inString = false;
		bool escaped = false;
		while (i < in.size() && depth > 0) {
			char c = in[i++];
			if (inString) {
				if (escaped)
					escaped = false;
				else if (c == '\\')
					escaped = true;
				else if (c == '"')
					inString = false;
				continue;
			}
			if (c == '"')
				inString = true;
			else if (c == open)
				++depth;
			else if (c == close)
				--depth;
		}
		return depth == 0;
	}
	static bool assignNumber(PerformanceSetup& setup, const std::string& key, double value) {
		for (const IntKey& f : kInts)
			if (key == f.key) {
				setup.*(f.member) = static_cast<int>(std::lround(value));
				return true;
			}
		for (const FloatKey& f : kFloats)
			if (key == f.key) {
				setup.*(f.member) = static_cast<float>(value);
				return true;
			}
		return false;
	}
};

bool directory(const std::string& path) {
	std::error_code ec;
	return std::filesystem::is_directory(path, ec);
}

} // namespace

PerformanceSetup captureSetup(const ParamBus& params, const FolderList& folders) {
	PerformanceSetup setup;
	for (const BoolKey& f : kBools)
		setup.*(f.member) = params.getBool(f.key, setup.*(f.member));
	for (const IntKey& f : kInts)
		setup.*(f.member) = params.getInt(f.key, setup.*(f.member));
	for (const FloatKey& f : kFloats)
		setup.*(f.member) = params.getFloat(f.key, setup.*(f.member));
	for (const StringKey& f : kStrings)
		setup.*(f.member) = params.getString(f.key, setup.*(f.member));
	setup.folders = folders.paths();
	return setup;
}

void applySetup(ParamBus& params, const PerformanceSetup& setup) {
	for (const BoolKey& f : kBools)
		params.setBool(f.key, setup.*(f.member));
	for (const IntKey& f : kInts)
		params.setInt(f.key, setup.*(f.member));
	for (const FloatKey& f : kFloats)
		params.setFloat(f.key, setup.*(f.member));
	for (const StringKey& f : kStrings)
		params.setString(f.key, setup.*(f.member));
	params.setBool("arm_record", false);
	params.setBool("cue_active", false);
	params.setBool("record_enabled", false);
}

bool writeSetup(const std::filesystem::path& path, const PerformanceSetup& setup) {
	return writeText(path, canonicalSetup(setup));
}

bool readSetup(const std::filesystem::path& path, PerformanceSetup& setup) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	std::string text((std::istreambuf_iterator<char>(in)), {});
	PerformanceSetup parsed;
	if (!Parser(text).parse(parsed)) return false;
	setup = std::move(parsed);
	return true;
}

RestoredFolders restoreFolders(const PerformanceSetup& setup) {
	RestoredFolders out;
	for (const std::string& path : setup.folders)
		if (directory(path)) out.paths.push_back(path);
	if (!setup.videos_folder.empty() && directory(setup.videos_folder)) {
		if (std::find(out.paths.begin(), out.paths.end(), setup.videos_folder) == out.paths.end())
			out.paths.push_back(setup.videos_folder);
		out.current = setup.videos_folder;
	}
	return out;
}

SetupSaver::SetupSaver(std::filesystem::path path, double quietSeconds) : file(std::move(path)), quiet(quietSeconds) {}

void SetupSaver::settle(const PerformanceSetup& setup) {
	saved = canonicalSetup(setup);
	pending = saved;
	waiting = false;
}

void SetupSaver::observe(const PerformanceSetup& setup, double nowSeconds) {
	std::string now = canonicalSetup(setup);
	if (now != pending) {
		pending = std::move(now);
		since = nowSeconds;
		waiting = true;
	}
	if (!waiting || pending == saved || nowSeconds - since < quiet) return;
	if (writeText(file, pending)) {
		saved = pending;
		waiting = false;
	}
}

void SetupSaver::flush() {
	if (pending == saved) return;
	if (writeText(file, pending)) {
		saved = pending;
		waiting = false;
	}
}
