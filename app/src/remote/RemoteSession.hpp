#pragma once

#include "remote/RemoteApply.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Serves the iPad page and its WebSocket behind a token. The network thread only reads the published
// snapshot, the token, and queued frames, and queues the sets it parses and the pad holds it stamps; callers own
// everything else.
class RemoteSession {
public:
	using TimePoint = std::chrono::steady_clock::time_point;
	struct PadState {
		bool gate;
		bool cue;
	};

	explicit RemoteSession(std::filesystem::path tokenFile);
	RemoteSession(std::filesystem::path tokenFile, std::string bindHost, uint16_t bindPort);
	~RemoteSession();
	RemoteSession(const RemoteSession&) = delete;
	RemoteSession& operator=(const RemoteSession&) = delete;

	void setEnabled(bool on);
	bool enabled() const { return listenFd >= 0; }
	// The first frame each new client receives.
	void setSnapshot(std::string json);
	// Queues one text frame for every connected client.
	void send(std::string frame);
	std::vector<RemoteSet> drain();
	// A pad is down while any client's latest hold on it is younger than the silence window.
	PadState pads() const;
	void setSilenceWindow(int ms) { silenceMs = ms; }
	void setClock(TimePoint (*fn)()) { clock = fn; }
	std::string token() const { return tok; }
	std::string url() const;
	int clientCount() const { return clients.load(); }
	uint16_t port() const { return boundPort; }

private:
	struct Client {
		int fd;
		std::string in;
	};
	struct Holds {
		TimePoint gate = TimePoint::min();
		TimePoint cue = TimePoint::min();
	};

	void serve();
	bool handle(int fd);
	bool readFrames(Client& client);
	bool waitReadable(int fd);
	void clearWake();

	std::string tok;
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
	std::mutex queueMutex;
	std::vector<RemoteSet> inbox;
	std::vector<std::string> outbox;
	mutable std::mutex holdMutex;
	std::map<int, Holds> holds; // keyed by client fd
	std::atomic<int> silenceMs{300};
	std::atomic<TimePoint (*)()> clock{&std::chrono::steady_clock::now};
};
