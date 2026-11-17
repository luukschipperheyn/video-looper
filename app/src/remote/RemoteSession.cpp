#include "remote/RemoteSession.hpp"

#include "remote/ControlSchema.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <mach-o/dyld.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>
#include <utility>
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

std::string serverFrame(unsigned char opcode, const std::string& payload) {
	std::string frame(1, char(0x80 | opcode));
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

bool isJsonValue(const std::string& raw) {
	if (raw == "true" || raw == "false" || raw[0] == '"') return true;
	if (raw[0] != '-' && !std::isdigit(static_cast<unsigned char>(raw[0]))) return false;
	char* end = nullptr;
	std::strtod(raw.c_str(), &end);
	return end == raw.c_str() + raw.size();
}

// The fields of one client message, each as its raw JSON token, or empty when absent.
struct Message {
	std::string type;
	std::string name;
	std::string value;
};

// Parses one flat JSON object such as {"t":"set","name":"...","value":<bool|number|string>} with keys in any order.
bool parseMessage(const std::string& json, Message& out) {
	size_t i = 0;
	auto skipSpace = [&] {
		while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
	};
	auto expect = [&](char c) {
		skipSpace();
		if (i >= json.size() || json[i] != c) return false;
		++i;
		return true;
	};
	auto token = [&](std::string& raw) {
		skipSpace();
		size_t start = i;
		if (i < json.size() && json[i] == '"') {
			for (++i; i < json.size() && json[i] != '"'; ++i) {
				if (static_cast<unsigned char>(json[i]) < 0x20) return false;
				if (json[i] == '\\') ++i;
			}
			if (i >= json.size()) return false;
			++i;
		} else {
			while (i < json.size() && (std::isalnum(static_cast<unsigned char>(json[i])) || json[i] == '-' ||
			                           json[i] == '+' || json[i] == '.'))
				++i;
		}
		raw = json.substr(start, i - start);
		return !raw.empty();
	};

	std::string key, raw;
	if (!expect('{')) return false;
	do {
		if (!token(key) || key[0] != '"' || !expect(':') || !token(raw)) return false;
		if (key == "\"t\"") out.type = raw;
		if (key == "\"name\"") out.name = raw;
		if (key == "\"value\"") out.value = raw;
	} while (expect(','));
	if (!expect('}')) return false;
	skipSpace();
	return i == json.size();
}

bool toSet(const Message& msg, RemoteSet& out) {
	const std::string& name = msg.name;
	if (name.size() < 3 || name[0] != '"' || name.find('\\') != std::string::npos) return false;
	if (msg.value.empty() || !isJsonValue(msg.value)) return false;
	out = {name.substr(1, name.size() - 2), msg.value};
	return true;
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
		shutdown(listenFd, SHUT_RDWR);
		(void)write(wakeWrite, "x", 1);
		thread.join();
		close(listenFd);
		close(wakeRead);
		close(wakeWrite);
		listenFd = wakeRead = wakeWrite = -1;
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
	int wake[2];
	if (!bound || listen(fd, 8) != 0 || pipe(wake) != 0) {
		close(fd);
		return;
	}
	fcntl(wake[0], F_SETFL, O_NONBLOCK);
	fcntl(wake[1], F_SETFL, O_NONBLOCK);
	socklen_t len = sizeof addr;
	getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
	boundPort = ntohs(addr.sin_port);
	listenFd = fd;
	wakeRead = wake[0];
	wakeWrite = wake[1];
	stop = false;
	thread = std::thread(&RemoteSession::serve, this);
}

void RemoteSession::setSnapshot(std::string json) {
	std::lock_guard<std::mutex> lock(snapshotMutex);
	snapshot = std::move(json);
}

void RemoteSession::send(std::string frame) {
	if (!enabled()) return;
	{
		std::lock_guard<std::mutex> lock(queueMutex);
		outbox.push_back(std::move(frame));
	}
	(void)write(wakeWrite, "x", 1);
}

std::vector<RemoteSet> RemoteSession::drain() {
	std::lock_guard<std::mutex> lock(queueMutex);
	return std::exchange(inbox, {});
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
	std::vector<Client> sockets;
	auto flush = [&] {
		std::vector<std::string> frames;
		{
			std::lock_guard<std::mutex> lock(queueMutex);
			frames.swap(outbox);
		}
		for (const std::string& f : frames) {
			std::string frame = serverFrame(0x1, f);
			for (const Client& c : sockets) sendAll(c.fd, frame);
		}
	};
	while (!stop) {
		flush();
		std::vector<pollfd> fds{{listenFd, POLLIN, 0}, {wakeRead, POLLIN, 0}};
		for (const Client& c : sockets) fds.push_back({c.fd, POLLIN, 0});
		if (poll(fds.data(), nfds_t(fds.size()), -1) <= 0) continue;
		if (fds[1].revents) clearWake();
		for (size_t i = fds.size() - 1; i >= 2; --i) {
			if (!fds[i].revents || readFrames(sockets[i - 2])) continue;
			{
				std::lock_guard<std::mutex> lock(holdMutex);
				holds.erase(fds[i].fd);
			}
			close(fds[i].fd);
			sockets.erase(sockets.begin() + long(i - 2));
		}
		if (fds[0].revents & POLLIN) {
			// Frames queued before this client connected belong to the clients already here.
			flush();
			int fd = accept(listenFd, nullptr, nullptr);
			if (fd >= 0 && handle(fd))
				sockets.push_back({fd, {}});
			else if (fd >= 0)
				close(fd);
		}
		clients = int(sockets.size());
	}
	{
		std::lock_guard<std::mutex> lock(holdMutex);
		holds.clear();
	}
	for (const Client& c : sockets) close(c.fd);
	clients = 0;
}

// Consumes every complete frame buffered for client. Returns false when the client should be closed.
bool RemoteSession::readFrames(Client& client) {
	constexpr uint64_t maxPayload = 65536;
	char buf[4096];
	ssize_t n = recv(client.fd, buf, sizeof buf, 0);
	if (n <= 0) return false;
	std::string& in = client.in;
	in.append(buf, size_t(n));
	auto byte = [&](size_t i) { return static_cast<unsigned char>(in[i]); };
	while (in.size() >= 2) {
		bool fin = byte(0) & 0x80;
		int opcode = byte(0) & 0x0f;
		if (!(byte(1) & 0x80)) return false;
		uint64_t len = byte(1) & 0x7f;
		size_t head = 2;
		if (len >= 126) {
			size_t extra = len == 126 ? 2 : 8;
			if (in.size() < head + extra) return true;
			len = 0;
			for (size_t i = 0; i < extra; ++i) len = (len << 8) | byte(head + i);
			head += extra;
		}
		if (len > maxPayload || (opcode >= 0x8 && len > 125)) return false;
		if (in.size() < head + 4 + len) return true;
		std::string payload = in.substr(head + 4, size_t(len));
		for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= in[head + i % 4];
		in.erase(0, head + 4 + size_t(len));
		if (opcode == 0x8) {
			sendAll(client.fd, serverFrame(0x8, ""));
			return false;
		}
		if (opcode == 0x9) sendAll(client.fd, serverFrame(0xA, payload));
		Message msg;
		if (opcode != 0x1 || !fin || !parseMessage(payload, msg)) continue;
		RemoteSet set;
		if (msg.type == "\"set\"" && toSet(msg, set)) {
			std::lock_guard<std::mutex> lock(queueMutex);
			inbox.push_back(std::move(set));
		} else if (msg.type == "\"hold\"" || msg.type == "\"release\"") {
			TimePoint at = msg.type == "\"hold\"" ? clock.load()() : TimePoint::min();
			std::lock_guard<std::mutex> lock(holdMutex);
			if (msg.name == "\"gate\"") holds[client.fd].gate = at;
			if (msg.name == "\"cue\"") holds[client.fd].cue = at;
		} else if (msg.type == "\"panic\"") {
			{
				std::lock_guard<std::mutex> lock(holdMutex);
				holds.clear();
			}
			std::lock_guard<std::mutex> lock(queueMutex);
			inbox.push_back({{}, {}, RemoteKind::Panic});
		}
	}
	return true;
}

RemoteSession::PadState RemoteSession::pads() const {
	TimePoint since = clock.load()() - std::chrono::milliseconds(silenceMs.load());
	PadState out{false, false};
	std::lock_guard<std::mutex> lock(holdMutex);
	for (const auto& [fd, h] : holds) {
		out.gate = out.gate || h.gate > since;
		out.cue = out.cue || h.cue > since;
	}
	return out;
}

// Waits up to 2 s for fd to become readable. Returns false on timeout or when the session stops.
bool RemoteSession::waitReadable(int fd) {
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!stop) {
		auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
		if (left.count() <= 0) return false;
		pollfd fds[] = {{fd, POLLIN, 0}, {wakeRead, POLLIN, 0}};
		if (poll(fds, 2, int(left.count())) < 0) return false;
		if (fds[0].revents) return true;
		if (fds[1].revents) clearWake();
	}
	return false;
}

void RemoteSession::clearWake() {
	char buf[64];
	while (read(wakeRead, buf, sizeof buf) > 0) {}
}

// Returns true when fd became a WebSocket client and should stay open.
bool RemoteSession::handle(int fd) {
	timeval timeout{2, 0};
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);

	std::string request;
	char buf[1024];
	while (request.find("\r\n\r\n") == std::string::npos) {
		if (request.size() > 8192 || !waitReadable(fd)) return false;
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
		                       acceptKey(key) + "\r\n\r\n" + serverFrame(0x1, snap));
	}
	if (method == "GET" && (path == "/" || path == "/index.html")) {
		std::ifstream in(webDir() / "index.html", std::ios::binary);
		if (in) {
			std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			const std::string placeholder = "/*CONTROL_SCHEMA*/null";
			size_t at = body.find(placeholder);
			if (at != std::string::npos) body.replace(at, placeholder.size(), controlSchemaJson());
			sendAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n"
			            "Content-Length: " +
			                std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
			return false;
		}
	}
	sendAll(fd, emptyResponse("404 Not Found"));
	return false;
}
