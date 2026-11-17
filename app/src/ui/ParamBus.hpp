#pragma once

#include <string>
#include <unordered_map>

class ParamBus {
public:
	void setBool(const std::string& key, bool v) { bools[key] = v; }
	void setFloat(const std::string& key, float v) { floats[key] = v; }
	void setString(const std::string& key, const std::string& v) { strings[key] = v; }
	void setInt(const std::string& key, int v) { ints[key] = v; }
	bool getBool(const std::string& key, bool def = false) const {
		auto it = bools.find(key);
		return it == bools.end() ? def : it->second;
	}
	float getFloat(const std::string& key, float def = 0.f) const {
		auto it = floats.find(key);
		return it == floats.end() ? def : it->second;
	}
	std::string getString(const std::string& key, const std::string& def = {}) const {
		auto it = strings.find(key);
		return it == strings.end() ? def : it->second;
	}
	int getInt(const std::string& key, int def = 0) const {
		auto it = ints.find(key);
		return it == ints.end() ? def : it->second;
	}

private:
	std::unordered_map<std::string, bool> bools;
	std::unordered_map<std::string, float> floats;
	std::unordered_map<std::string, std::string> strings;
	std::unordered_map<std::string, int> ints;
};
