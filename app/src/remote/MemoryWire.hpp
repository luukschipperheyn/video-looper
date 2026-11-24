#pragma once

#include "remote/Wire.hpp"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

class MemoryWire : public Wire {
public:
	using TimePoint = std::chrono::steady_clock::time_point;

	void push(std::string json);
	void pushDraw(std::string png);
	const std::vector<std::string>& sent() const { return out; }
	std::string thumb(const std::string& name) const;

	void setEnabled(bool on) override;
	bool enabled() const override;
	void setTokenRequired(bool on) override;
	void setSilenceWindow(int ms) override;
	void setSnapshot(std::string json) override;
	void send(std::string frame) override;
	void setThumbs(std::vector<std::pair<std::string, std::string>> nameAndBytes) override;
	std::vector<Inbound> takeMessages() override;
	bool takeDraw(std::string& png) override;
	PadHolds pads() const override;
	void releaseHolds() override;
	std::string url() const override;
	int clientCount() const override;

private:
	bool listening = false;
	int silenceMs = 300;
	std::vector<Inbound> inbox;
	std::vector<std::string> out;
	std::string snapshot;
	std::string draw;
	bool drawFresh = false;
	std::vector<std::pair<std::string, std::string>> thumbs;
	TimePoint gate = TimePoint::min();
	TimePoint cue = TimePoint::min();
	TimePoint erase = TimePoint::min();
};
