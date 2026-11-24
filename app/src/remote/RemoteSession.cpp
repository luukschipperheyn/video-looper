#include "remote/RemoteSession.hpp"

#include "control/ControlTable.hpp"

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
#include <cerrno>
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
	for (int i = 0; i < 32; ++i)
		token += "0123456789abcdef"[rd() & 0xf];
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream(file) << token << '\n';
	std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, ec);
	return token;
}

bool tokenMatches(const std::string& given, const std::string& expected) {
	if (given.size() != expected.size()) return false;
	unsigned char diff = 0;
	for (size_t i = 0; i < given.size(); ++i)
		diff |= static_cast<unsigned char>(given[i] ^ expected[i]);
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

std::string percentDecode(const std::string& s) {
	std::string out;
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
		    std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
			out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
			i += 2;
		} else {
			out += s[i];
		}
	}
	return out;
}

std::string lower(std::string s) {
	for (char& c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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
		for (int i = 7; i >= 0; --i)
			frame += char((n >> (8 * i)) & 0xff);
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

bool isSnapshot(const std::string& json) {
	return json.rfind("{\"t\":\"snapshot\"", 0) == 0;
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

RemoteSession::~RemoteSession() {
	setEnabled(false);
}

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
		if (isSnapshot(frame)) std::erase_if(outbox, isSnapshot);
		outbox.push_back(std::move(frame));
	}
	(void)write(wakeWrite, "x", 1);
}

void RemoteSession::setThumbs(std::vector<std::pair<std::string, std::string>> nameAndBytes) {
	std::map<std::string, std::string> next(std::make_move_iterator(nameAndBytes.begin()),
	                                        std::make_move_iterator(nameAndBytes.end()));
	std::lock_guard<std::mutex> lock(thumbMutex);
	thumbs.swap(next);
}

std::vector<Inbound> RemoteSession::takeMessages() {
	std::lock_guard<std::mutex> lock(queueMutex);
	return std::exchange(inbox, {});
}

bool RemoteSession::hasMessages() {
	std::lock_guard<std::mutex> lock(queueMutex);
	return !inbox.empty();
}

void RemoteSession::releaseHolds() {
	std::lock_guard<std::mutex> lock(holdMutex);
	holds.clear();
}

bool RemoteSession::takeDraw(std::string& png) {
	std::lock_guard<std::mutex> lock(queueMutex);
	if (!drawFresh) return false;
	drawFresh = false;
	png = std::move(drawBytes);
	drawBytes.clear();
	return !png.empty();
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
	std::string url = "http://" + host + ":" + std::to_string(boundPort) + "/";
	if (requireToken.load()) url += "?token=" + tok;
	return url;
}

void RemoteSession::serve() {
	std::vector<Client> sockets;
	auto closeAt = [&](size_t i) {
		{
			std::lock_guard<std::mutex> lock(holdMutex);
			holds.erase(sockets[i].fd);
		}
		close(sockets[i].fd);
		sockets.erase(sockets.begin() + long(i));
	};
	// Writes what it can without waiting. A full socket keeps the tail; the caller closes a dead one.
	auto drain = [&](Client& c) {
		while (!c.out.empty()) {
			ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), 0);
			if (n > 0) {
				c.out.erase(0, size_t(n));
				continue;
			}
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return true;
			return false;
		}
		return true;
	};
	auto queueFrames = [&] {
		std::vector<std::string> frames;
		{
			std::lock_guard<std::mutex> lock(queueMutex);
			frames.swap(outbox);
		}
		int last = -1;
		for (int i = 0; i < int(frames.size()); ++i)
			if (isSnapshot(frames[size_t(i)])) last = i;
		if (last >= 0) {
			std::vector<std::string> kept;
			for (int i = 0; i < int(frames.size()); ++i)
				if (!isSnapshot(frames[size_t(i)]) || i == last) kept.push_back(std::move(frames[size_t(i)]));
			frames.swap(kept);
		}
		for (Client& c : sockets) {
			// A client that has not finished the previous snapshot does not get another piled behind it.
			bool behind = !c.out.empty();
			for (const std::string& f : frames) {
				if (behind && isSnapshot(f)) continue;
				c.out += serverFrame(0x1, f);
			}
		}
	};
	while (!stop) {
		queueFrames();
		for (size_t i = sockets.size(); i-- > 0;) {
			if (!drain(sockets[i])) closeAt(i);
		}
		std::vector<pollfd> fds{{listenFd, POLLIN, 0}, {wakeRead, POLLIN, 0}};
		for (const Client& c : sockets)
			fds.push_back({c.fd, short(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0});
		if (poll(fds.data(), nfds_t(fds.size()), -1) <= 0) continue;
		if (fds[1].revents) clearWake();
		for (size_t i = fds.size() - 1; i >= 2; --i) {
			size_t si = i - 2;
			short ev = fds[i].revents;
			if (!ev) continue;
			if ((ev & POLLIN) && !readFrames(sockets[si])) {
				closeAt(si);
				continue;
			}
			if ((ev & (POLLERR | POLLHUP | POLLNVAL)) && !(ev & POLLIN)) {
				closeAt(si);
				continue;
			}
			if ((ev & POLLOUT) && !drain(sockets[si])) closeAt(si);
		}
		if (fds[0].revents & POLLIN) {
			// Frames queued before this client connected belong to the clients already here.
			queueFrames();
			for (size_t i = sockets.size(); i-- > 0;) {
				if (!drain(sockets[i])) closeAt(i);
			}
			int fd = accept(listenFd, nullptr, nullptr);
			if (fd >= 0 && handle(fd)) {
				int flags = fcntl(fd, F_GETFL, 0);
				if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
				sockets.push_back({fd, {}, {}});
			} else if (fd >= 0)
				close(fd);
		}
		clients = int(sockets.size());
	}
	{
		std::lock_guard<std::mutex> lock(holdMutex);
		holds.clear();
	}
	for (const Client& c : sockets)
		close(c.fd);
	clients = 0;
}

// Consumes every complete frame buffered for client. Returns false when the client should be closed.
// A binary frame is a Draw PNG. Text frames stay at 64 KB; a Draw picture may be up to 4 MB.
bool RemoteSession::readFrames(Client& client) {
	constexpr uint64_t textLimit = 65536;
	constexpr uint64_t drawLimit = 4ull * 1024ull * 1024ull;
	std::string& in = client.in;
	bool closed = false;
	for (;;) {
		char buf[65536];
		ssize_t n = recv(client.fd, buf, sizeof buf, 0);
		if (n < 0) {
			if (errno == EINTR) continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK) break;
			return false;
		}
		if (n == 0) {
			closed = true;
			break;
		}
		in.append(buf, size_t(n));
	}
	auto byte = [&](size_t i) { return static_cast<unsigned char>(in[i]); };
	auto storeDraw = [&](std::string png) {
		if (png.empty()) return;
		std::lock_guard<std::mutex> lock(queueMutex);
		drawBytes = std::move(png);
		drawFresh = true;
	};
	while (in.size() >= 2) {
		bool fin = byte(0) & 0x80;
		int opcode = byte(0) & 0x0f;
		if (!(byte(1) & 0x80)) return false;
		uint64_t len = byte(1) & 0x7f;
		size_t head = 2;
		if (len >= 126) {
			size_t extra = len == 126 ? 2 : 8;
			if (in.size() < head + extra) return !closed;
			len = 0;
			for (size_t i = 0; i < extra; ++i)
				len = (len << 8) | byte(head + i);
			head += extra;
		}
		uint64_t limit = (opcode == 0x2 || opcode == 0x0) ? drawLimit : textLimit;
		if (len > limit || (opcode >= 0x8 && len > 125)) return false;
		if (in.size() < head + 4 + len) return !closed;
		std::string payload = in.substr(head + 4, size_t(len));
		for (size_t i = 0; i < payload.size(); ++i)
			payload[i] ^= in[head + i % 4];
		in.erase(0, head + 4 + size_t(len));
		if (opcode == 0x8) {
			sendAll(client.fd, serverFrame(0x8, ""));
			return false;
		}
		if (opcode == 0x9) sendAll(client.fd, serverFrame(0xA, payload));
		if (opcode == 0x0) {
			if (client.fragOp != 0x2) continue;
			if (client.frag.size() + payload.size() > drawLimit) return false;
			client.frag += payload;
			if (!fin) continue;
			storeDraw(std::move(client.frag));
			client.frag.clear();
			client.fragOp = -1;
			continue;
		}
		if (opcode == 0x2) {
			if (!fin) {
				client.frag = std::move(payload);
				client.fragOp = 0x2;
				continue;
			}
			client.frag.clear();
			client.fragOp = -1;
			storeDraw(std::move(payload));
			continue;
		}
		if (opcode != 0x1 || !fin) continue;
		ParsedInbound got = parseInbound(payload);
		if (got.kind == InboundKind::Bad) continue;
		if (got.kind == InboundKind::Hold || got.kind == InboundKind::Release) {
			TimePoint at = got.kind == InboundKind::Hold ? clock.load()() : TimePoint::min();
			std::lock_guard<std::mutex> lock(holdMutex);
			if (got.pad == "gate") holds[client.fd].gate = at;
			if (got.pad == "cue") holds[client.fd].cue = at;
			if (got.pad == "erase") holds[client.fd].erase = at;
			continue;
		}
		std::lock_guard<std::mutex> lock(queueMutex);
		inbox.push_back(std::move(got.message));
	}
	return !closed;
}

PadHolds RemoteSession::pads() const {
	TimePoint since = clock.load()() - std::chrono::milliseconds(silenceMs.load());
	PadState out{false, false, false};
	std::lock_guard<std::mutex> lock(holdMutex);
	for (const auto& [fd, h] : holds) {
		out.gate = out.gate || h.gate > since;
		out.cue = out.cue || h.cue > since;
		out.erase = out.erase || h.erase > since;
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

	if (requireToken.load() && !tokenMatches(queryParam(query, "token"), tok)) {
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
	if (method == "GET" && path == "/thumb") {
		std::string body;
		bool found = false;
		{
			std::lock_guard<std::mutex> lock(thumbMutex);
			auto it = thumbs.find(percentDecode(queryParam(query, "name")));
			if (it != thumbs.end()) {
				body = it->second;
				found = true;
			}
		}
		sendAll(fd, found ? "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nCache-Control: no-store\r\n"
		                    "Content-Length: " +
		                        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body
		                  : emptyResponse("404 Not Found"));
		return false;
	}
	if (method == "GET" && (path == "/" || path == "/index.html")) {
		std::ifstream in(webDir() / "index.html", std::ios::binary);
		if (in) {
			std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			const std::string gesturesMark = "/*GESTURES*/";
			size_t gestureAt = body.find(gesturesMark);
			if (gestureAt != std::string::npos) {
				std::ifstream gesturesIn(webDir() / "gestures.js", std::ios::binary);
				std::string gestures((std::istreambuf_iterator<char>(gesturesIn)), std::istreambuf_iterator<char>());
				body.replace(gestureAt, gesturesMark.size(), gestures);
			}
			const std::string pageStateMark = "/*PAGE_STATE*/";
			size_t pageStateAt = body.find(pageStateMark);
			if (pageStateAt != std::string::npos) {
				std::ifstream pageStateIn(webDir() / "page_state.js", std::ios::binary);
				std::string pageState((std::istreambuf_iterator<char>(pageStateIn)), std::istreambuf_iterator<char>());
				body.replace(pageStateAt, pageStateMark.size(), pageState);
			}
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
