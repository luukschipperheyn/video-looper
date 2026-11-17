#include "remote/ControlSchema.hpp"
#include "remote/RemoteApply.hpp"
#include "remote/RemoteSession.hpp"
#include "ui/ParamBus.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

// A WebSocket client: fd plus bytes received but not yet consumed.
struct Ws {
	int fd = -1;
	std::string buf;
};

static bool fill(Ws& ws, size_t count) {
	char chunk[4096];
	while (ws.buf.size() < count) {
		ssize_t n = recv(ws.fd, chunk, sizeof chunk, 0);
		if (n <= 0) return false;
		ws.buf.append(chunk, size_t(n));
	}
	return true;
}

static bool readHead(Ws& ws, std::string& head) {
	size_t headEnd;
	while ((headEnd = ws.buf.find("\r\n\r\n")) == std::string::npos)
		if (!fill(ws, ws.buf.size() + 1)) return false;
	head = ws.buf.substr(0, headEnd + 4);
	ws.buf.erase(0, headEnd + 4);
	return true;
}

// Reads one unmasked server frame; opcode keeps the FIN bit.
static bool readFrame(Ws& ws, int& opcode, std::string& payload) {
	if (!fill(ws, 2)) return false;
	opcode = static_cast<unsigned char>(ws.buf[0]);
	uint64_t len = static_cast<unsigned char>(ws.buf[1]) & 0x7f;
	size_t offset = 2;
	if (len == 126 || len == 127) {
		size_t extra = len == 126 ? 2 : 8;
		if (!fill(ws, 2 + extra)) return false;
		len = 0;
		for (size_t i = 0; i < extra; ++i) len = (len << 8) | static_cast<unsigned char>(ws.buf[2 + i]);
		offset += extra;
	}
	if (!fill(ws, offset + len)) return false;
	payload = ws.buf.substr(offset, len);
	ws.buf.erase(0, offset + len);
	return true;
}

static std::string readText(Ws& ws) {
	int opcode = 0;
	std::string payload;
	return readFrame(ws, opcode, payload) && opcode == 0x81 ? payload : std::string();
}

// Sends one masked client frame. A nonzero split sends the first split bytes, pauses, then the rest.
static void sendFrame(int fd, int opcode, const std::string& payload, size_t split = 0) {
	const unsigned char mask[4] = {0x12, 0x34, 0x56, 0x78};
	std::string frame(1, char(0x80 | opcode));
	uint64_t n = payload.size();
	if (n < 126) {
		frame += char(0x80 | n);
	} else if (n < 65536) {
		frame += char(0x80 | 126);
		frame += char(n >> 8);
		frame += char(n & 0xff);
	} else {
		frame += char(0x80 | 127);
		for (int i = 7; i >= 0; --i) frame += char((n >> (8 * i)) & 0xff);
	}
	frame.append(reinterpret_cast<const char*>(mask), 4);
	for (size_t i = 0; i < payload.size(); ++i) frame += char(payload[i] ^ mask[i % 4]);
	if (split == 0 || split >= frame.size()) split = frame.size();
	send(fd, frame.data(), split, 0);
	if (split == frame.size()) return;
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	send(fd, frame.data() + split, frame.size() - split, 0);
}

static std::string setFrame(const std::string& name, const std::string& raw) {
	return "{\"t\":\"set\",\"name\":\"" + name + "\",\"value\":" + raw + "}";
}

// The raw JSON token of a top-level field, or empty when absent.
static std::string field(const std::string& json, const std::string& name) {
	std::string key = "\"" + name + "\":";
	size_t at = json.find(key);
	if (at == std::string::npos) return {};
	at += key.size();
	return json.substr(at, json.find_first_of(",}", at) - at);
}

static bool isSnapshot(const std::string& payload) { return payload.rfind("{\"t\":\"snapshot\"", 0) == 0; }

static std::vector<RemoteSet> waitDrain(RemoteSession& session) {
	for (int i = 0; i < 100; ++i) {
		std::vector<RemoteSet> sets = session.drain();
		if (!sets.empty()) return sets;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return {};
}

static void publish(RemoteSession& session, const ParamBus& params) {
	std::string snapshot = remoteSnapshot(params);
	session.setSnapshot(snapshot);
	session.send(snapshot);
}

// One App frame: apply what the page sent, send rejects and the snapshot. Returns the tempo App would hand
// to Link, or -1 when it would not touch Link.
static float pump(RemoteSession& session, ParamBus& params, bool linkOn = false) {
	float linkTempo = -1.0f;
	for (const RemoteSet& set : waitDrain(session)) {
		ApplyResult result = applyRemoteSet(params, set);
		if (result.reject) session.send(rejectFrame(set.name, result.reason));
		if (result.tempoChanged && linkOn) linkTempo = result.bpm;
	}
	publish(session, params);
	return linkTempo;
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
		const std::string bpmSchema = R"("bpm":{"type":"float","min":40,"max":240,"writable":true})";
		check(res.find("/*CONTROL_SCHEMA*/") == std::string::npos && res.find(bpmSchema) != std::string::npos,
		      "3. page carries the control schema");

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
		Ws ws{fd, {}};
		std::string head, payload;
		int opcode = 0;
		bool got = readHead(ws, head) && readFrame(ws, opcode, payload);
		check(got && head.rfind("HTTP/1.1 101", 0) == 0, "5. WebSocket upgrade with token returns 101");
		check(head.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos,
		      "5. Sec-WebSocket-Accept matches RFC 6455 example");
		check(got && opcode == 0x81 && payload == snapshot, "5. first frame is a text frame equal to the snapshot");
		check(waitFor(session, 1), "client is counted while connected");

		RemoteSession second(tokenFile, "127.0.0.1", 0);
		check(second.token() == token && token.size() == 32, "6. second session on the same token file shares token");

		close(fd);
		check(waitFor(session, 0), "7. clientCount returns to 0 after the client closes");

		ParamBus params;
		params.setFloat("bpm", 120.0f);
		publish(session, params);
		Ws client{connectTo(port), {}};
		send(client.fd, req.data(), req.size(), 0);
		got = readHead(client, head);
		payload = readText(client);
		check(got && isSnapshot(payload) && field(payload, "bpm") == "120",
		      "8. a new client first reads the published snapshot");
		check(field(payload, "commit_active").empty() && field(payload, "commit_random").empty() &&
		          field(payload, "commit_current_file").empty(),
		      "8. Commit keys are absent from the snapshot");

		sendFrame(client.fd, 0x9, "hi");
		got = readFrame(client, opcode, payload);
		check(got && opcode == 0x8A && payload == "hi", "8. ping gets a pong with the same payload");

		for (const Control& c : kControls) {
			std::string name = c.name;
			if (!c.writable || name == "record_enabled") continue;
			double mid = c.min + 0.63 * (c.max - c.min);
			std::string sent;
			if (c.type == ControlType::Bool) {
				sent = params.getBool(name) ? "false" : "true";
			} else if (c.type == ControlType::Int) {
				sent = std::to_string(std::lround(mid));
			} else {
				char buf[32];
				std::snprintf(buf, sizeof buf, "%g", mid);
				sent = buf;
			}
			sendFrame(client.fd, 0x1, setFrame(name, sent));
			pump(session, params);
			std::string echoed = field(readText(client), name);
			bool same = echoed == sent;
			if (c.type == ControlType::Float)
				same = !echoed.empty() &&
				       std::strtof(echoed.c_str(), nullptr) == float(std::strtod(sent.c_str(), nullptr));
			check(same, ("9. " + name + " set to " + sent + " echoes " + echoed).c_str());
		}

		sendFrame(client.fd, 0x1, setFrame("bpm", "999"));
		pump(session, params);
		check(field(readText(client), "bpm") == "240", "10. bpm 999 comes back clamped to 240");
		sendFrame(client.fd, 0x1, setFrame("webcam_index", "-3"));
		pump(session, params);
		check(field(readText(client), "webcam_index") == "0", "10. webcam_index -3 comes back clamped to 0");

		params.setFloat("quantum", 4.0f);
		const std::pair<std::string, std::string> readOnly[] = {{"record_path", "\"/tmp/evil.mov\""},
		                                                        {"quantum", "8"},
		                                                        {"link_enabled", "true"},
		                                                        {"commit_active", "true"}};
		for (const auto& [name, raw] : readOnly) {
			sendFrame(client.fd, 0x1, setFrame(name, raw));
			pump(session, params);
			std::string reject = readText(client);
			payload = readText(client);
			check(reject == "{\"t\":\"reject\",\"name\":\"" + name + "\",\"reason\":\"read-only\"}" &&
			          isSnapshot(payload),
			      ("11. " + name + " is rejected as read-only").c_str());
		}
		check(params.getString("record_path").empty() && params.getFloat("quantum") == 4.0f &&
		          !params.getBool("link_enabled") && !params.getBool("commit_active"),
		      "11. read-only sets leave ParamBus unchanged");
		check(field(payload, "commit_active").empty(), "11. commit_active stays absent from the snapshot");

		params.setFloat("bpm", 133.5f);
		publish(session, params);
		check(field(readText(client), "bpm") == "133.5", "12. a bpm change on the Mac reaches the page");

		sendFrame(client.fd, 0x1, setFrame("bpm", "150"));
		float linkTempo = pump(session, params, true);
		check(linkTempo == 150.0f && field(readText(client), "bpm") == "150",
		      "13. with Link on, setting bpm hands 150 to Link");

		sendFrame(client.fd, 0x1, setFrame("record_enabled", "true"));
		pump(session, params);
		std::string reject = readText(client);
		payload = readText(client);
		check(reject == R"({"t":"reject","name":"record_enabled","reason":"no export path"})",
		      "14. Export with no path is rejected with no export path");
		check(!params.getBool("record_enabled") && field(payload, "record_enabled") == "false",
		      "14. the snapshot says Export is not running");
		params.setString("record_path", "/tmp/vl-export.mov");
		sendFrame(client.fd, 0x1, setFrame("record_enabled", "true"));
		pump(session, params);
		check(field(readText(client), "record_enabled") == "true",
		      "14. with a path set, the snapshot says Export is running");

		std::string big = setFrame("gain_mix", "0.25");
		big.insert(big.size() - 1, std::string(10000, ' '));
		sendFrame(client.fd, 0x1, big, 100);
		pump(session, params);
		check(field(readText(client), "gain_mix") == "0.25", "15. a set larger than one recv still parses");

		sendFrame(client.fd, 0x1, R"({"t":"set","name":"bpm","value":})");
		sendFrame(client.fd, 0x1, R"({"t":"set","name":"bpm")");
		sendFrame(client.fd, 0x1, "not json");
		sendFrame(client.fd, 0x1, setFrame("gain_mix", "0.5"));
		std::vector<RemoteSet> sets = waitDrain(session);
		check(sets.size() == 1 && sets[0].name == "gain_mix" && sets[0].raw == "0.5",
		      "16. bad JSON is dropped, not queued");

		int pending = connectTo(port);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		session.setEnabled(false);
		check(!session.enabled() && session.clientCount() == 0, "17. disabling returns with a handshake pending");
		close(pending);
		close(client.fd);
	}
	std::filesystem::remove_all(dir);
	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
