#include "setup/SetupStore.hpp"

#include "control/ControlTable.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <utility>

namespace {

bool saved(const Control& c) {
	return c.keep == Keep::Saved;
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
	for (const Control& c : kControls) {
		if (!saved(c)) continue;
		out += jsonString(c.name);
		out += ':';
		switch (c.type) {
		case ControlType::Bool:
			out += setup.values.getBool(c.name) ? "true" : "false";
			break;
		case ControlType::Int:
			out += std::to_string(setup.values.getInt(c.name));
			break;
		case ControlType::Float:
			out += jsonFloat(setup.values.getFloat(c.name));
			break;
		case ControlType::String:
			out += jsonString(setup.values.getString(c.name));
			break;
		}
		out += ',';
	}
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
			const Control* control = findControl(key);
			if (key == "folders") {
				if (!parseStringArray(setup.folders)) return false;
			} else if (!control || !saved(*control)) {
				if (!skipValue()) return false;
			} else if (control->type == ControlType::Bool) {
				bool value;
				if (!parseBool(value)) return false;
				setup.values.setBool(key, value);
			} else if (control->type == ControlType::String) {
				std::string value;
				if (!parseString(value)) return false;
				setup.values.setString(key, value);
			} else {
				double value;
				if (!parseNumber(value)) return false;
				if (control->type == ControlType::Int)
					setup.values.setInt(key, static_cast<int>(std::lround(value)));
				else
					setup.values.setFloat(key, static_cast<float>(value));
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
};

bool directory(const std::string& path) {
	std::error_code ec;
	return std::filesystem::is_directory(path, ec);
}

} // namespace

// Copies the Saved controls from one bus to another.
static void copySaved(const ParamBus& from, ParamBus& to) {
	for (const Control& c : kControls) {
		if (!saved(c)) continue;
		switch (c.type) {
		case ControlType::Bool:
			to.setBool(c.name, from.getBool(c.name));
			break;
		case ControlType::Int:
			to.setInt(c.name, from.getInt(c.name));
			break;
		case ControlType::Float:
			to.setFloat(c.name, from.getFloat(c.name));
			break;
		case ControlType::String:
			to.setString(c.name, from.getString(c.name));
			break;
		}
	}
}

PerformanceSetup captureSetup(const ParamBus& params, const FolderList& folders) {
	PerformanceSetup setup;
	copySaved(params, setup.values);
	setup.folders = folders.paths();
	return setup;
}

void applySetup(ParamBus& params, const PerformanceSetup& setup) {
	copySaved(setup.values, params);
	for (const Control& c : kControls)
		if (c.keep == Keep::StartsOff) params.setBool(c.name, c.def != 0.0f);
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
	std::string current = setup.values.getString("videos_folder");
	if (!current.empty() && directory(current)) {
		if (std::find(out.paths.begin(), out.paths.end(), current) == out.paths.end()) out.paths.push_back(current);
		out.current = current;
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
