#include "util/HotReload.hpp"

bool HotReload::watch(const std::string& path) {
	pathToMtime[path] = getMtime(path);
	return true;
}

bool HotReload::changed(const std::string& path) {
	auto nowM = getMtime(path);
	auto it = pathToMtime.find(path);
	if (it == pathToMtime.end()) {
		pathToMtime[path] = nowM;
		return false;
	}
	if (nowM != it->second && nowM != 0) {
		it->second = nowM;
		return true;
	}
	return false;
}


