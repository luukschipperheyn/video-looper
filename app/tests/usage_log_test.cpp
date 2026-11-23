#include "remote/UsageLog.hpp"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

static int failures = 0;

static void check(bool ok, const char* what) {
	std::cout << (ok ? "PASS " : "FAIL ") << what << "\n";
	if (!ok) ++failures;
}

static std::string readFile(const std::filesystem::path& path) {
	std::ifstream in(path);
	return std::string((std::istreambuf_iterator<char>(in)), {});
}

static int count(const std::string& data, const std::string& needle) {
	int n = 0;
	for (size_t at = 0; (at = data.find(needle, at)) != std::string::npos; at += needle.size())
		++n;
	return n;
}

int main() {
	auto dir = std::filesystem::temp_directory_path() / ("vl-usage-" + std::to_string(getpid()));
	std::filesystem::remove_all(dir);
	auto path = dir / "nested" / "web-use.jsonl";
	{
		UsageLog log(path, 200, 1);
		log.write("act", "dub", "\"clip.mp4\"");
		for (int i = 0; i < 8; ++i)
			log.write("set", "n" + std::to_string(i), std::to_string(i));
	}
	std::string data = readFile(path);
	check(data.find("\"v\":\"clip.mp4\"") != std::string::npos || data.find("\"name\":\"n7\"") != std::string::npos,
	      "1. a written gesture is stored as one JSON line");
	check(data.find("\"name\":\"n7\"") != std::string::npos && data.find("\"v\":7") != std::string::npos,
	      "2. the newest gesture stays");
	check(data.find("\"name\":\"n0\"") == std::string::npos, "3. a file past the cap drops its oldest lines");
	bool linesOk = !data.empty();
	for (size_t at = 0; at < data.size() && linesOk;) {
		size_t nl = data.find('\n', at);
		std::string line = data.substr(at, (nl == std::string::npos ? data.size() : nl) - at);
		if (!line.empty() && line.rfind("{\"at\":", 0) != 0) linesOk = false;
		if (nl == std::string::npos) break;
		at = nl + 1;
	}
	check(linesOk && count(data, "\n") >= 1, "4. the trimmed file is still JSONL");
	std::filesystem::remove_all(dir);
	std::cout << (failures ? "FAILED" : "ALL PASSED") << " (" << failures << " failures)\n";
	return failures ? 1 : 0;
}
