#pragma once

#include "remote/Wire.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class RemoteSession : public Wire {
public:
	using TimePoint = std::chrono::steady_clock::time_point;
	using PadState = PadHolds;

	explicit RemoteSession(std::filesystem::path tokenFile);
	RemoteSession(std::filesystem::path tokenFile, std::string bindHost, uint16_t bindPort);
	~RemoteSession() override;
	RemoteSession(const RemoteSession&) = delete;
	RemoteSession& operator=(const RemoteSession&) = delete;

	void setEnabled(bool on) override;
	bool enabled() const override { return listenFd >= 0; }
	void setTokenRequired(bool on) override { requireToken.store(on); }
	bool tokenRequired() const { return requireToken.load(); }
	void setSnapshot(std::string json) override;
	void send(std::string frame) override;
	void setThumbs(std::vector<std::pair<std::string, std::string>> nameAndBytes) override;
	std::vector<Inbound> takeMessages() override;
	bool hasMessages();
	bool takeDraw(std::string& png) override;
	PadHolds pads() const override;
	void releaseHolds() override;
	void setSilenceWindow(int ms) override { silenceMs = ms; }
	void setClock(TimePoint (*fn)()) { clock = fn; }
	std::string token() const { return tok; }
	std::string url() const override;
	int clientCount() const override { return clients.load(); }
	uint16_t port() const { return boundPort; }

private:
	struct Client {
		int fd;
		std::string in;
		std::string out;
		std::string frag;
		int fragOp = -1;
	};
	struct Holds {
		TimePoint gate = TimePoint::min();
		TimePoint cue = TimePoint::min();
		TimePoint erase = TimePoint::min();
	};

	void serve();
	bool handle(int fd);
	bool readFrames(Client& client);
	bool waitReadable(int fd);
	void clearWake();

	std::string tok;
	std::atomic<bool> requireToken{true};
	std::string bindHost;
	uint16_t bindPort;
	uint16_t boundPort = 0;
	int listenFd = -1;
	int wakeRead = -1;
	int wakeWrite = -1;
	std::thread thread;
	std::atomic<bool> stop{false};
	std::atomic<int> clients{0};
	mutable std::mutex snapshotMutex;
	std::string snapshot = "{}";
	std::mutex thumbMutex;
	std::map<std::string, std::string> thumbs;
	std::mutex queueMutex;
	std::vector<Inbound> inbox;
	std::string drawBytes;
	bool drawFresh = false;
	std::vector<std::string> outbox;
	mutable std::mutex holdMutex;
	std::map<int, Holds> holds;
	std::atomic<int> silenceMs{300};
	std::atomic<TimePoint (*)()> clock{&std::chrono::steady_clock::now};
};
