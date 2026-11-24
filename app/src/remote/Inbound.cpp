#include "remote/Inbound.hpp"

#include <cctype>
#include <cstdlib>

namespace {

bool isJsonValue(const std::string& raw) {
	if (raw == "true" || raw == "false" || raw[0] == '"') return true;
	if (raw[0] != '-' && !std::isdigit(static_cast<unsigned char>(raw[0]))) return false;
	char* end = nullptr;
	std::strtod(raw.c_str(), &end);
	return end == raw.c_str() + raw.size();
}

struct Message {
	std::string type;
	std::string event;
	std::string name;
	std::string value;
	std::vector<std::string> names;
};

bool plainString(const std::string& raw, std::string& out) {
	if (raw.size() < 3 || raw[0] != '"' || raw.find('\\') != std::string::npos) return false;
	out = raw.substr(1, raw.size() - 2);
	return true;
}

bool parseMessage(const std::string& json, Message& out) {
	size_t i = 0;
	auto skipSpace = [&] {
		while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i])))
			++i;
	};
	auto expect = [&](char c) {
		skipSpace();
		if (i >= json.size() || json[i] != c) return false;
		++i;
		return true;
	};
	auto token = [&](std::string& raw) {
		skipSpace();
		size_t start = i;
		if (i < json.size() && json[i] == '"') {
			for (++i; i < json.size() && json[i] != '"'; ++i) {
				if (static_cast<unsigned char>(json[i]) < 0x20) return false;
				if (json[i] == '\\') ++i;
			}
			if (i >= json.size()) return false;
			++i;
		} else {
			while (i < json.size() && (std::isalnum(static_cast<unsigned char>(json[i])) || json[i] == '-' ||
			                           json[i] == '+' || json[i] == '.'))
				++i;
		}
		raw = json.substr(start, i - start);
		return !raw.empty();
	};
	auto stringArray = [&](std::vector<std::string>& dest) {
		if (!expect('[')) return false;
		skipSpace();
		if (i < json.size() && json[i] == ']') {
			++i;
			return true;
		}
		do {
			std::string item;
			std::string plain;
			if (!token(item) || !plainString(item, plain)) return false;
			dest.push_back(std::move(plain));
		} while (expect(','));
		return expect(']');
	};

	std::string key, raw;
	if (!expect('{')) return false;
	do {
		if (!token(key) || key[0] != '"' || !expect(':')) return false;
		skipSpace();
		if (i < json.size() && json[i] == '[') {
			std::vector<std::string> items;
			if (!stringArray(items)) return false;
			if (key == "\"names\"") out.names = std::move(items);
		} else {
			if (!token(raw)) return false;
			if (key == "\"t\"") out.type = raw;
			if (key == "\"e\"") out.event = raw;
			if (key == "\"name\"") out.name = raw;
			if (key == "\"value\"" || key == "\"v\"") out.value = raw;
		}
	} while (expect(','));
	if (!expect('}')) return false;
	skipSpace();
	return i == json.size();
}

}

ParsedInbound parseInbound(const std::string& json) {
	Message msg;
	ParsedInbound out;
	if (!parseMessage(json, msg)) return out;
	std::string type;
	if (!plainString(msg.type, type)) return out;
	if (type == "hold" || type == "release") {
		if (!plainString(msg.name, out.pad)) return out;
		out.kind = type == "hold" ? InboundKind::Hold : InboundKind::Release;
		return out;
	}
	out.kind = InboundKind::Message;
	out.message.type = std::move(type);
	if (out.message.type == "set") {
		if (!plainString(msg.name, out.message.name) || msg.value.empty() || !isJsonValue(msg.value)) return {};
		out.message.raw = msg.value;
		return out;
	}
	if (out.message.type == "load" || out.message.type == "folder" || out.message.type == "dub") {
		if (!plainString(msg.name, out.message.name)) return {};
		return out;
	}
	if (out.message.type == "use") {
		plainString(msg.event, out.message.event);
		plainString(msg.name, out.message.name);
		out.message.raw = msg.value;
		return out;
	}
	if (out.message.type == "page") {
		out.message.names = std::move(msg.names);
		return out;
	}
	return out;
}
