#include "util/HotReload.hpp"

bool HotReload::watch(const std::string& path) {
	pathToMtime[path] = getMtime(path);
	return true;
}

std::unordered_set<std::string> HotReload::poll() {
	std::unordered_set<std::string> changed;
	for (auto& entry : pathToMtime) {
		time_t nowM = getMtime(entry.first);
		if (nowM != entry.second && nowM != 0) {
			entry.second = nowM;
			changed.insert(entry.first);
		}
	}
	return changed;
}
