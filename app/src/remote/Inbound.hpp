#pragma once

#include <string>
#include <vector>

struct Inbound {
	std::string type;
	std::string name;
	std::string raw;
	std::string event;
	std::vector<std::string> names;
};

enum class InboundKind { Bad, Hold, Release, Message };

struct ParsedInbound {
	InboundKind kind = InboundKind::Bad;
	std::string pad;
	Inbound message;
};

ParsedInbound parseInbound(const std::string& json);
