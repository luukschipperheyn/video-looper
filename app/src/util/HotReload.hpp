#pragma once

#include <string>
#include <unordered_map>
#include <sys/stat.h>

class HotReload {
public:
	bool watch(const std::string& path);
	bool changed(const std::string& path);

private:
	std::unordered_map<std::string, time_t> pathToMtime;
	static time_t getMtime(const std::string& path) {
		struct stat s{};
		if (stat(path.c_str(), &s) == 0) {
			return s.st_mtime;
		}
		return 0;
	}
};
