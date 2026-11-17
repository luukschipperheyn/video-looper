#include "remote/RemoteSession.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

static int failures = 0;

static void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) ++failures;
}

static int connectTo(uint16_t port) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
	if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
		close(fd);
		return -1;
	}
	timeval timeout{3, 0};
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
	return fd;
}

static std::string readAll(int fd) {
	std::string out;
	char buf[4096];
	ssize_t n;
	while ((n = recv(fd, buf, sizeof buf, 0)) > 0) out.append(buf, size_t(n));
	return out;
}

static std::string httpGet(uint16_t port, const std::string& target) {
	int fd = connectTo(port);
	if (fd < 0) return {};
	std::string req = "GET " + target + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
	send(fd, req.data(), req.size(), 0);
	std::string res = readAll(fd);
	close(fd);
	return res;
}

static std::string upgradeRequest(const std::string& target) {
	return "GET " + target +
	       " HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
	       "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
}

// Reads the handshake response and one unmasked server frame from fd.
static bool readHandshakeAndFrame(int fd, std::string& head, int& opcode, std::string& payload) {
	std::string buf;
	char chunk[4096];
	size_t headEnd;
	while ((headEnd = buf.find("\r\n\r\n")) == std::string::npos) {
		ssize_t n = recv(fd, chunk, sizeof chunk, 0);
		if (n <= 0) return false;
		buf.append(chunk, size_t(n));
	}
	head = buf.substr(0, headEnd + 4);
	std::string rest = buf.substr(headEnd + 4);
	auto need = [&](size_t count) {
		while (rest.size() < count) {
			ssize_t n = recv(fd, chunk, sizeof chunk, 0);
			if (n <= 0) return false;
			rest.append(chunk, size_t(n));
		}
		return true;
	};
	if (!need(2)) return false;
	opcode = static_cast<unsigned char>(rest[0]);
	uint64_t len = static_cast<unsigned char>(rest[1]) & 0x7f;
	size_t offset = 2;
	if (len == 126 || len == 127) {
		size_t extra = len == 126 ? 2 : 8;
		if (!need(2 + extra)) return false;
		len = 0;
		for (size_t i = 0; i < extra; ++i) len = (len << 8) | static_cast<unsigned char>(rest[2 + i]);
		offset += extra;
	}
	if (!need(offset + len)) return false;
	payload = rest.substr(offset, len);
	return true;
}

static bool waitFor(const RemoteSession& session, int count) {
	for (int i = 0; i < 100; ++i) {
		if (session.clientCount() == count) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	return false;
}

int main() {
	auto dir = std::filesystem::temp_directory_path() / ("vl-remote-test-" + std::to_string(getpid()));
	std::filesystem::remove_all(dir);
	auto tokenFile = dir / "remote-token";
	{
		RemoteSession session(tokenFile, "127.0.0.1", 0);
		const std::string token = session.token();

		session.setEnabled(true);
		uint16_t port = session.port();
		check(session.enabled() && port != 0, "enabled session listens on an ephemeral port");
		session.setEnabled(false);
		int fd = connectTo(port);
		check(fd < 0 && !session.enabled(), "1. disabled session refuses TCP connections");
		if (fd >= 0) close(fd);

		session.setEnabled(true);
		port = session.port();

		std::string res = httpGet(port, "/");
		check(res.rfind("HTTP/1.1 403", 0) == 0, "2. GET / without token returns 403");
		check(res.find("id=\"connection\"") == std::string::npos, "2. 403 carries no page body");
		res = httpGet(port, "/?token=" + std::string(32, '0'));
		check(res.rfind("HTTP/1.1 403", 0) == 0, "2. GET / with a wrong token returns 403");

		res = httpGet(port, "/?token=" + token);
		check(res.rfind("HTTP/1.1 200", 0) == 0, "3. GET /?token returns 200");
		check(res.find("id=\"connection\"") != std::string::npos, "3. page body contains id=\"connection\"");

		fd = connectTo(port);
		std::string req = upgradeRequest("/ws");
		send(fd, req.data(), req.size(), 0);
		res = readAll(fd);
		close(fd);
		check(res.rfind("HTTP/1.1 403", 0) == 0 && res.find(" 101 ") == std::string::npos,
		      "4. WebSocket upgrade without token is refused");

		const std::string snapshot = R"({"hello":"ipad","n":1})";
		session.setSnapshot(snapshot);
		fd = connectTo(port);
		req = upgradeRequest("/ws?token=" + token);
		send(fd, req.data(), req.size(), 0);
		std::string head, payload;
		int opcode = 0;
		bool got = readHandshakeAndFrame(fd, head, opcode, payload);
		check(got && head.rfind("HTTP/1.1 101", 0) == 0, "5. WebSocket upgrade with token returns 101");
		check(head.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos,
		      "5. Sec-WebSocket-Accept matches RFC 6455 example");
		check(got && opcode == 0x81 && payload == snapshot, "5. first frame is a text frame equal to the snapshot");
		check(waitFor(session, 1), "client is counted while connected");

		RemoteSession second(tokenFile, "127.0.0.1", 0);
		check(second.token() == token && token.size() == 32, "6. second session on the same token file shares token");

		close(fd);
		check(waitFor(session, 0), "7. clientCount returns to 0 after the client closes");
	}
	std::filesystem::remove_all(dir);
	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
