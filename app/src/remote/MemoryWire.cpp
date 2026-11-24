#include "remote/MemoryWire.hpp"

#include <utility>

void MemoryWire::push(std::string json) {
	ParsedInbound got = parseInbound(json);
	if (got.kind == InboundKind::Bad) return;
	if (got.kind == InboundKind::Hold || got.kind == InboundKind::Release) {
		TimePoint at = got.kind == InboundKind::Hold ? std::chrono::steady_clock::now() : TimePoint::min();
		if (got.pad == "gate") gate = at;
		if (got.pad == "cue") cue = at;
		if (got.pad == "erase") erase = at;
		return;
	}
	inbox.push_back(std::move(got.message));
}

void MemoryWire::pushDraw(std::string png) {
	if (png.empty()) return;
	draw = std::move(png);
	drawFresh = true;
}

std::string MemoryWire::thumb(const std::string& name) const {
	for (const auto& [thumbName, bytes] : thumbs)
		if (thumbName == name) return bytes;
	return {};
}

void MemoryWire::setEnabled(bool on) {
	listening = on;
}

bool MemoryWire::enabled() const {
	return listening;
}

void MemoryWire::setTokenRequired(bool) {}

void MemoryWire::setSilenceWindow(int ms) {
	silenceMs = ms;
}

void MemoryWire::setSnapshot(std::string json) {
	snapshot = std::move(json);
}

void MemoryWire::send(std::string frame) {
	out.push_back(std::move(frame));
}

void MemoryWire::setThumbs(std::vector<std::pair<std::string, std::string>> nameAndBytes) {
	thumbs = std::move(nameAndBytes);
}

std::vector<Inbound> MemoryWire::takeMessages() {
	return std::exchange(inbox, {});
}

bool MemoryWire::takeDraw(std::string& png) {
	if (!drawFresh) return false;
	drawFresh = false;
	png = std::move(draw);
	draw.clear();
	return !png.empty();
}

PadHolds MemoryWire::pads() const {
	TimePoint since = std::chrono::steady_clock::now() - std::chrono::milliseconds(silenceMs);
	return {gate > since, cue > since, erase > since};
}

void MemoryWire::releaseHolds() {
	gate = cue = erase = TimePoint::min();
}

std::string MemoryWire::url() const {
	return {};
}

int MemoryWire::clientCount() const {
	return 0;
}
