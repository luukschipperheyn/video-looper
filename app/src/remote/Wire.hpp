#pragma once

#include "remote/Inbound.hpp"

#include <string>
#include <utility>
#include <vector>

struct PadHolds {
	bool gate = false;
	bool cue = false;
	bool erase = false;
};

class Wire {
public:
	virtual ~Wire() = default;
	virtual void setEnabled(bool on) = 0;
	virtual bool enabled() const = 0;
	virtual void setTokenRequired(bool on) = 0;
	virtual void setSilenceWindow(int ms) = 0;
	virtual void setSnapshot(std::string json) = 0;
	virtual void send(std::string frame) = 0;
	virtual void setThumbs(std::vector<std::pair<std::string, std::string>> nameAndBytes) = 0;
	virtual std::vector<Inbound> takeMessages() = 0;
	virtual bool takeDraw(std::string& png) = 0;
	virtual PadHolds pads() const = 0;
	virtual void releaseHolds() = 0;
	virtual std::string url() const = 0;
	virtual int clientCount() const = 0;
};
