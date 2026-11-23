#pragma once

#include "remote/RemoteApply.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class UsageLog;

// Serves the iPad page and its WebSocket. The URL token is required until setTokenRequired(false). The network
// thread only reads the published snapshot, thumbnails, the token, that flag, and queued frames, and queues the
// sets it parses and the pad holds it stamps; callers own everything else.
class RemoteSession {
public:
	using TimePoint = std::chrono::steady_clock::time_point;
	struct PadState {
		bool gate;
		bool cue;
		bool erase;
	};

	explicit RemoteSession(std::filesystem::path tokenFile);
	RemoteSession(std::filesystem::path tokenFile, std::string bindHost, uint16_t bindPort);
	~RemoteSession();
	RemoteSession(const RemoteSession&) = delete;
	RemoteSession& operator=(const RemoteSession&) = delete;

	void setEnabled(bool on);
	bool enabled() const { return listenFd >= 0; }
	// While required, a request whose token does not match is refused. The shown URL omits ?token= when it is not.
	void setTokenRequired(bool on) { requireToken.store(on); }
	bool tokenRequired() const { return requireToken.load(); }
	// The first frame each new client receives.
	void setSnapshot(std::string json);
	// Queues one text frame for every connected client. A newer snapshot replaces one that is still queued,
	// so a slow page cannot pile snapshots up and stall the pad holds.
	void send(std::string frame);
	// Replaces the JPEGs served at /thumb?name=, keyed by Video filename.
	void setThumbs(std::vector<std::pair<std::string, std::string>> nameAndBytes);
	std::vector<RemoteSet> drain();
	// A pad is down while any client's latest hold on it is younger than the silence window.
	PadState pads() const;
	void setSilenceWindow(int ms) { silenceMs = ms; }
	// Call before setEnabled. The network thread appends Web interface gestures to this file.
	// An empty path records nothing. Control messages are not written; the page sends a use for each gesture.
	void setUsageFile(std::filesystem::path path) { usagePath = std::move(path); }
	void setClock(TimePoint (*fn)()) { clock = fn; }
	std::string token() const { return tok; }
	std::string url() const;
	int clientCount() const { return clients.load(); }
	uint16_t port() const { return boundPort; }

private:
	struct Client {
		int fd;
		std::string in;
		std::string out;
	};
	struct Holds {
		TimePoint gate = TimePoint::min();
		TimePoint cue = TimePoint::min();
		TimePoint erase = TimePoint::min();
	};

	void serve();
	void noteUse(const std::string& event, const std::string& name, const std::string& value);
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
	std::vector<RemoteSet> inbox;
	std::vector<std::string> outbox;
	mutable std::mutex holdMutex;
	std::map<int, Holds> holds; // keyed by client fd
	std::atomic<int> silenceMs{300};
	std::atomic<TimePoint (*)()> clock{&std::chrono::steady_clock::now};
	std::filesystem::path usagePath;
	std::unique_ptr<UsageLog> usage; // created on the network thread; destroyed after that thread has joined
};
