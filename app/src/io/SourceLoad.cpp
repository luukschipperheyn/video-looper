#include "io/SourceLoad.hpp"

#include <algorithm>
#include <filesystem>

std::string stepVideo(const std::vector<std::string>& files, const std::string& current, int delta) {
	if (files.empty()) return {};
	long long n = static_cast<long long>(files.size());
	auto it = std::find(files.begin(), files.end(), current);
	long long from = it != files.end() ? it - files.begin() : (delta < 0 ? n : -1);
	return files[static_cast<size_t>(((from + delta) % n + n) % n)];
}

std::string videoByName(const std::vector<std::string>& files, const std::string& name) {
	auto it = std::find_if(files.begin(), files.end(),
	                       [&](const std::string& f) { return std::filesystem::path(f).filename() == name; });
	return it != files.end() ? *it : std::string();
}

std::vector<std::string> videosByName(const std::vector<std::string>& files, const std::vector<std::string>& names) {
	std::vector<std::string> out;
	for (const std::string& name : names) {
		std::string path = videoByName(files, name);
		if (!path.empty()) out.push_back(std::move(path));
	}
	return out;
}
