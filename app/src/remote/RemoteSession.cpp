#include "remote/RemoteSession.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <mach-o/dyld.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <fstream>
#include <iterator>
#include <random>
#include <vector>

namespace {

bool isToken(const std::string& s) {
	return s.size() == 32 &&
	       std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::string loadOrCreateToken(const std::filesystem::path& file) {
	std::string token;
	{
		std::ifstream in(file);
		in >> token;
	}
	if (isToken(token)) return token;
	std::random_device rd;
	token.clear();
	for (int i = 0; i < 32; ++i) token += "0123456789abcdef"[rd() & 0xf];
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream(file) << token << '\n';
	std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, ec);
	return token;
}

bool tokenMatches(const std::string& given, const std::string& expected) {
	if (given.size() != expected.size()) return false;
	unsigned char diff = 0;
	for (size_t i = 0; i < given.size(); ++i) diff |= static_cast<unsigned char>(given[i] ^ expected[i]);
	return diff == 0;
}

std::string queryParam(const std::string& query, const std::string& key) {
	size_t pos = 0;
	while (pos <= query.size()) {
		size_t end = std::min(query.find('&', pos), query.size());
		std::string pair = query.substr(pos, end - pos);
		if (pair.compare(0, key.size() + 1, key + "=") == 0) return pair.substr(key.size() + 1);
		pos = end + 1;
	}
	return {};
}

std::string lower(std::string s) {
	for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

// name must be lowercase; the value keeps its case.
std::string header(const std::string& request, const std::string& name) {
	size_t pos = lower(request).find("\r\n" + name + ":");
	if (pos == std::string::npos) return {};
	pos += name.size() + 3;
	size_t end = request.find("\r\n", pos);
	std::string value = request.substr(pos, end - pos);
	value.erase(0, value.find_first_not_of(" \t"));
	value.erase(value.find_last_not_of(" \t") + 1);
	return value;
}

std::string base64(const unsigned char* data, size_t len) {
	const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	for (size_t i = 0; i < len; i += 3) {
		uint32_t n = uint32_t(data[i]) << 16;
		if (i + 1 < len) n |= uint32_t(data[i + 1]) << 8;
		if (i + 2 < len) n |= data[i + 2];
		out += table[(n >> 18) & 63];
		out += table[(n >> 12) & 63];
		out += i + 1 < len ? table[(n >> 6) & 63] : '=';
		out += i + 2 < len ? table[n & 63] : '=';
	}
	return out;
}

std::string acceptKey(const std::string& key) {
	std::string s = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	unsigned char digest[CC_SHA1_DIGEST_LENGTH];
	CC_SHA1(s.data(), static_cast<CC_LONG>(s.size()), digest);
	return base64(digest, sizeof digest);
}

std::string textFrame(const std::string& payload) {
	std::string frame(1, char(0x81));
	uint64_t n = payload.size();
	if (n < 126) {
		frame += char(n);
	} else if (n < 65536) {
		frame += char(126);
		frame += char(n >> 8);
		frame += char(n & 0xff);
	} else {
		frame += char(127);
		for (int i = 7; i >= 0; --i) frame += char((n >> (8 * i)) & 0xff);
	}
	return frame + payload;
}

std::string emptyResponse(const std::string& status) {
	return "HTTP/1.1 " + status + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
}

bool sendAll(int fd, const std::string& data) {
	size_t off = 0;
	while (off < data.size()) {
		ssize_t n = send(fd, data.data() + off, data.size() - off, 0);
		if (n <= 0) return false;
		off += size_t(n);
	}
	return true;
}

std::filesystem::path webDir() {
	char exe[PATH_MAX];
	uint32_t size = sizeof exe;
	if (_NSGetExecutablePath(exe, &size) == 0) {
		auto bundled = std::filesystem::path(exe).parent_path() / "../Resources/web";
		if (std::filesystem::exists(bundled / "index.html")) return bundled;
	}
	return WEB_DIR;
}

} // namespace

RemoteSession::RemoteSession(std::filesystem::path tokenFile) : RemoteSession(std::move(tokenFile), "0.0.0.0", 8734) {}

RemoteSession::RemoteSession(std::filesystem::path tokenFile, std::string bindHost, uint16_t bindPort)
	: tok(loadOrCreateToken(tokenFile)), bindHost(std::move(bindHost)), bindPort(bindPort) {}

RemoteSession::~RemoteSession() { setEnabled(false); }

void RemoteSession::setEnabled(bool on) {
	if (on == enabled()) return;
	if (!on) {
		stop = true;
		thread.join();
		close(listenFd);
		listenFd = -1;
		return;
	}
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	if (inet_pton(AF_INET, bindHost.c_str(), &addr.sin_addr) != 1) return;
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return;
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	bool bound = false;
	int attempts = bindPort == 0 ? 1 : 100;
	for (int i = 0; i < attempts && !bound; ++i) {
		addr.sin_port = htons(uint16_t(bindPort + i));
		bound = bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0;
	}
	if (!bound || listen(fd, 8) != 0) {
		close(fd);
		return;
	}
	socklen_t len = sizeof addr;
	getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
	boundPort = ntohs(addr.sin_port);
	listenFd = fd;
	stop = false;
	thread = std::thread(&RemoteSession::serve, this);
}

void RemoteSession::setSnapshot(std::string json) {
	std::lock_guard<std::mutex> lock(snapshotMutex);
	snapshot = std::move(json);
}

std::string RemoteSession::url() const {
	std::string host = bindHost;
	if (host == "0.0.0.0") {
		host = "127.0.0.1";
		ifaddrs* list = nullptr;
		if (getifaddrs(&list) == 0) {
			for (ifaddrs* a = list; a; a = a->ifa_next) {
				if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
				if ((a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP)) continue;
				char buf[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, buf, sizeof buf);
				host = buf;
				break;
			}
			freeifaddrs(list);
		}
	}
	return "http://" + host + ":" + std::to_string(boundPort) + "/?token=" + tok;
}

void RemoteSession::serve() {
	std::vector<int> sockets;
	while (!stop) {
		std::vector<pollfd> fds{{listenFd, POLLIN, 0}};
		for (int s : sockets) fds.push_back({s, POLLIN, 0});
		if (poll(fds.data(), nfds_t(fds.size()), 50) <= 0) continue;
		for (size_t i = fds.size() - 1; i >= 1; --i) {
			if (!fds[i].revents) continue;
			unsigned char buf[512];
			ssize_t n = recv(fds[i].fd, buf, sizeof buf, 0);
			if (n > 0 && (buf[0] & 0x0f) != 0x8) continue;
			if (n > 0) sendAll(fds[i].fd, std::string("\x88\x00", 2));
			close(fds[i].fd);
			sockets.erase(sockets.begin() + long(i - 1));
		}
		if (fds[0].revents & POLLIN) {
			int fd = accept(listenFd, nullptr, nullptr);
			if (fd >= 0 && handle(fd))
				sockets.push_back(fd);
			else if (fd >= 0)
				close(fd);
		}
		clients = int(sockets.size());
	}
	for (int s : sockets) close(s);
	clients = 0;
}

// Returns true when fd became a WebSocket client and should stay open.
bool RemoteSession::handle(int fd) {
	timeval timeout{2, 0};
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);

	std::string request;
	char buf[1024];
	while (request.find("\r\n\r\n") == std::string::npos) {
		if (request.size() > 8192) return false;
		ssize_t n = recv(fd, buf, sizeof buf, 0);
		if (n <= 0) return false;
		request.append(buf, size_t(n));
	}
	size_t sp1 = request.find(' ');
	size_t sp2 = sp1 == std::string::npos ? sp1 : request.find(' ', sp1 + 1);
	if (sp2 == std::string::npos) return false;
	std::string method = request.substr(0, sp1);
	std::string target = request.substr(sp1 + 1, sp2 - sp1 - 1);
	size_t q = target.find('?');
	std::string path = target.substr(0, q);
	std::string query = q == std::string::npos ? "" : target.substr(q + 1);

	if (!tokenMatches(queryParam(query, "token"), tok)) {
		sendAll(fd, emptyResponse("403 Forbidden"));
		return false;
	}
	if (method == "GET" && path == "/ws" && lower(header(request, "upgrade")) == "websocket") {
		std::string key = header(request, "sec-websocket-key");
		if (key.empty()) {
			sendAll(fd, emptyResponse("400 Bad Request"));
			return false;
		}
		std::string snap;
		{
			std::lock_guard<std::mutex> lock(snapshotMutex);
			snap = snapshot;
		}
		return sendAll(fd, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		                   "Sec-WebSocket-Accept: " +
		                       acceptKey(key) + "\r\n\r\n" + textFrame(snap));
	}
	if (method == "GET" && (path == "/" || path == "/index.html")) {
		std::ifstream in(webDir() / "index.html", std::ios::binary);
		if (in) {
			std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			sendAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n"
			            "Content-Length: " +
			                std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
			return false;
		}
	}
	sendAll(fd, emptyResponse("404 Not Found"));
	return false;
}
