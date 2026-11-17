#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

// Serves the iPad page and its WebSocket behind a token. The network thread only
// reads the published snapshot and the token; callers own everything else.
class RemoteSession {
public:
	explicit RemoteSession(std::filesystem::path tokenFile);
	RemoteSession(std::filesystem::path tokenFile, std::string bindHost, uint16_t bindPort);
	~RemoteSession();
	RemoteSession(const RemoteSession&) = delete;
	RemoteSession& operator=(const RemoteSession&) = delete;

	void setEnabled(bool on);
	bool enabled() const { return listenFd >= 0; }
	void setSnapshot(std::string json);
	std::string token() const { return tok; }
	std::string url() const;
	int clientCount() const { return clients.load(); }
	uint16_t port() const { return boundPort; }

private:
	void serve();
	bool handle(int fd);

	std::string tok;
	std::string bindHost;
	uint16_t bindPort;
	uint16_t boundPort = 0;
	int listenFd = -1;
	std::thread thread;
	std::atomic<bool> stop{false};
	std::atomic<int> clients{0};
	mutable std::mutex snapshotMutex;
	std::string snapshot = "{}";
};
