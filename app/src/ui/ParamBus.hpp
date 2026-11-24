#pragma once

#include <string>
#include <unordered_map>

// The current value of every control. A fresh bus holds the defaults from the control table, so a getter needs
// no fallback. A name the table does not list reads as false, 0, or empty.
class ParamBus {
public:
	ParamBus();

	void setBool(const std::string& key, bool v) { bools[key] = v; }
	void setFloat(const std::string& key, float v) { floats[key] = v; }
	void setString(const std::string& key, const std::string& v) { strings[key] = v; }
	void setInt(const std::string& key, int v) { ints[key] = v; }
	bool getBool(const std::string& key) const {
		auto it = bools.find(key);
		return it == bools.end() ? false : it->second;
	}
	float getFloat(const std::string& key) const {
		auto it = floats.find(key);
		return it == floats.end() ? 0.f : it->second;
	}
	std::string getString(const std::string& key) const {
		auto it = strings.find(key);
		return it == strings.end() ? std::string() : it->second;
	}
	int getInt(const std::string& key) const {
		auto it = ints.find(key);
		return it == ints.end() ? 0 : it->second;
	}

private:
	std::unordered_map<std::string, bool> bools;
	std::unordered_map<std::string, float> floats;
	std::unordered_map<std::string, std::string> strings;
	std::unordered_map<std::string, int> ints;
};
