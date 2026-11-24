#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <sys/stat.h>

class HotReload {
public:
	bool watch(const std::string& path);
	std::unordered_set<std::string> poll();

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
