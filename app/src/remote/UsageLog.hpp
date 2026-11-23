#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

// Append-only record of Web interface gestures, one JSON object per line.
// A long file keeps its newest lines so the record stays bounded.
class UsageLog {
public:
	UsageLog(std::filesystem::path path, std::uintmax_t capBytes = 4u * 1024u * 1024u, int trimEvery = 64);
	// valueJson is a JSON token (number, bool, or string) or empty when the gesture has no value.
	void write(std::string_view event, std::string_view name, std::string_view valueJson);

private:
	void trim();
	std::filesystem::path path;
	std::ofstream out;
	std::uintmax_t cap;
	int trimEvery;
	int sinceTrim = 0;
};
