#include "io/FolderList.hpp"
#include "io/SourceLoad.hpp"
#include "remote/ControlSchema.hpp"
#include "remote/RemoteApply.hpp"
#include "remote/RemoteSession.hpp"
#include "ui/ParamBus.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
	while ((n = recv(fd, buf, sizeof buf, 0)) > 0)
		out.append(buf, size_t(n));
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
		for (size_t i = 0; i < extra; ++i)
			len = (len << 8) | static_cast<unsigned char>(ws.buf[2 + i]);
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
		for (int i = 7; i >= 0; --i)
			frame += char((n >> (8 * i)) & 0xff);
	}
	frame.append(reinterpret_cast<const char*>(mask), 4);
	for (size_t i = 0; i < payload.size(); ++i)
		frame += char(payload[i] ^ mask[i % 4]);
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

static bool isSnapshot(const std::string& payload) {
	return payload.rfind("{\"t\":\"snapshot\"", 0) == 0;
}

static std::vector<RemoteSet> waitDrain(RemoteSession& session) {
	for (int i = 0; i < 100; ++i) {
		std::vector<RemoteSet> sets = session.drain();
		if (!sets.empty()) return sets;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return {};
}

static void publish(RemoteSession& session, const ParamBus& params, const RemoteLive& live = {}) {
	std::string snapshot = remoteSnapshot(params, live);
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

static bool waitPads(const RemoteSession& session, bool gate, bool cue, bool erase = false) {
	for (int i = 0; i < 100; ++i) {
		RemoteSession::PadState p = session.pads();
		if (p.gate == gate && p.cue == cue && p.erase == erase) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return false;
}

static std::atomic<RemoteSession::TimePoint> fakeNow{RemoteSession::TimePoint(std::chrono::hours(1))};
static RemoteSession::TimePoint fakeClock() {
	return fakeNow.load();
}

static Ws openClient(uint16_t port, const std::string& token) {
	Ws ws{connectTo(port), {}};
	std::string req = upgradeRequest("/ws?token=" + token), head;
	send(ws.fd, req.data(), req.size(), 0);
	readHead(ws, head);
	readText(ws);
	return ws;
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
		check(res.find("id=\"two-hands\"") == std::string::npos, "2. 403 carries no page body");
		res = httpGet(port, "/?token=" + std::string(32, '0'));
		check(res.rfind("HTTP/1.1 403", 0) == 0, "2. GET / with a wrong token returns 403");

		res = httpGet(port, "/?token=" + token);
		check(res.rfind("HTTP/1.1 200", 0) == 0, "3. GET /?token returns 200");
		check(res.find("id=\"two-hands\"") != std::string::npos, "3. page body contains id=\"two-hands\"");
		const std::string bpmSchema = R"("bpm":{"type":"float","min":40,"max":240,"writable":true})";
		check(res.find("/*CONTROL_SCHEMA*/") == std::string::npos && res.find(bpmSchema) != std::string::npos,
		      "3. page carries the control schema");
		check(res.find("relativeDrag") != std::string::npos, "3. page carries the gesture module");

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

		params.setFloat("video_sx", 2.0f);
		params.setFloat("video_sy", 0.5f);
		sendFrame(client.fd, 0x1, setFrame("video_aspect", "true"));
		pump(session, params);
		check(field(readText(client), "video_sy") == "2" && params.getFloat("video_sy") == 2.0f,
		      "10. Preserve aspect copies scale x onto scale y");

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

	check(combineHolds({false, false, true, false, false, false}).gate, "18. a pad alone opens the gate");
	check(combineHolds({false, true, true, false, false, false}).gate &&
	          combineHolds({false, true, false, false, false, false}).gate,
	      "18. Space plus a pad stays open when the pad releases");
	check(!combineHolds({false, false, false, false, false, false}).gate, "18. both released closes the gate");
	check(combineHolds({true, false, false, false, false, false}).gate, "18. Latch keeps the gate open with no pad");
	check(combineHolds({false, false, false, false, false, true}).cue &&
	          !combineHolds({false, false, false, false, false, true}).gate,
	      "18. a Cue pad opens Cue and not the gate");
	{
		bool latched = false;
		check(overdubIsBlack(true, false, false, true, 1, latched) && latched, "18. Erase writes black");
		check(overdubIsBlack(false, false, false, true, 4, latched), "18. the release tail stays black");
		check(!overdubIsBlack(false, false, false, true, 0, latched) && !latched, "18. idle drops the black image");
		check(overdubIsBlack(true, true, false, true, 3, latched), "18. Erase wins over a Source overdub");
		check(!overdubIsBlack(false, true, false, true, 3, latched) && !latched,
		      "18. a Source overdub takes the image back");
		latched = true;
		check(!overdubIsBlack(false, false, true, true, 4, latched) && !latched, "18. Cue takes the image back");
		latched = true;
		check(!overdubIsBlack(false, false, false, false, 4, latched) && !latched,
		      "18. a disabled envelope has no black tail");
	}
	{
		RemoteSession session(tokenFile, "127.0.0.1", 0);
		session.setClock(fakeClock);
		session.setEnabled(true);
		uint16_t port = session.port();
		ParamBus params;
		Ws client = openClient(port, session.token());

		sendFrame(client.fd, 0x1, R"({"t":"hold","name":"gate"})");
		check(waitPads(session, true, false), "19. a gate hold opens the gate pad");
		RemoteLive live;
		live.gate = session.pads().gate;
		publish(session, params, live);
		check(field(readText(client), "gate") == "true", "19. the client reads \"gate\":true");
		sendFrame(client.fd, 0x1, R"({"t":"hold","name":"cue"})");
		check(waitPads(session, true, true), "19. a cue hold opens the Cue pad");
		live.cue = session.pads().cue;
		publish(session, params, live);
		check(field(readText(client), "cue") == "true", "19. the client reads \"cue\":true");

		fakeNow = fakeNow.load() + std::chrono::milliseconds(301);
		RemoteSession::PadState p = session.pads();
		check(!p.gate && !p.cue, "20. with no hold for longer than the silence window both pads drop");
		sendFrame(client.fd, 0x1, R"({"t":"hold","name":"gate"})");
		check(waitPads(session, true, false), "20. a new hold reopens the gate pad");
		sendFrame(client.fd, 0x1, R"({"t":"release","name":"gate"})");
		check(waitPads(session, false, false), "20. a release drops the gate pad without waiting");
		sendFrame(client.fd, 0x1, R"({"t":"release","name":"cue"})");
		session.setSilenceWindow(1000);
		sendFrame(client.fd, 0x1, R"({"t":"hold","name":"gate"})");
		check(waitPads(session, true, false), "20. a hold under a 1000 ms window opens the gate pad");
		fakeNow = fakeNow.load() + std::chrono::milliseconds(500);
		check(session.pads().gate, "20. the gate pad stays down 500 ms later under a 1000 ms window");
		sendFrame(client.fd, 0x1, R"({"t":"release","name":"gate"})");
		check(waitPads(session, false, false), "20. release clears the gate pad");
		session.setSilenceWindow(300);

		params.setBool("arm_record", true);
		Ws other = openClient(port, session.token());
		sendFrame(other.fd, 0x1, R"({"t":"hold","name":"gate"})");
		sendFrame(client.fd, 0x1, R"({"t":"hold","name":"cue"})");
		check(waitPads(session, true, true), "21. holds from two clients are both down");
		close(other.fd);
		check(waitPads(session, false, true), "21. closing one socket drops only that client's pads");
		check(params.getBool("arm_record"), "21. the Latch stays on after the socket closes");
		sendFrame(client.fd, 0x1, R"({"t":"release","name":"cue"})");
		check(waitPads(session, false, false), "21. the other client's release drops its Cue pad");

		params.setBool("cue_active", true);
		params.setFloat("bpm", 128.0f);
		sendFrame(client.fd, 0x1, R"({"t":"hold","name":"gate"})");
		sendFrame(client.fd, 0x1, R"({"t":"hold","name":"cue"})");
		sendFrame(client.fd, 0x1, R"({"t":"hold","name":"erase"})");
		check(waitPads(session, true, true, true), "22. the pads are down before panic");
		sendFrame(client.fd, 0x1, R"({"t":"panic"})");
		std::vector<RemoteSet> sets = waitDrain(session);
		for (const RemoteSet& set : sets) {
			if (set.kind != RemoteKind::Panic) continue;
			params.setBool("arm_record", false);
			params.setBool("cue_active", false);
		}
		p = session.pads();
		check(sets.size() == 1 && sets[0].kind == RemoteKind::Panic, "22. panic drains as one Panic item");
		check(!params.getBool("arm_record") && !params.getBool("cue_active") && !p.gate && !p.cue && !p.erase,
		      "22. after panic the Latch, the latched Cue, and the pads are clear");
		check(params.getFloat("bpm") == 128.0f, "22. panic leaves other params alone");
		sendFrame(client.fd, 0x1, R"({"t":"hold","name":"erase"})");
		check(waitPads(session, false, false, true), "22. an Erase hold opens Erase and not the gate");
		sendFrame(client.fd, 0x1, R"({"t":"release","name":"erase"})");
		check(waitPads(session, false, false, false), "22. releasing Erase drops it");

		params.setString("file_path", "/tmp/videos/clip.mov");
		params.setInt("source_kind", 1);
		publish(session, params, {0.25f, 0.5f, 3, true, false, true});
		std::string payload = readText(client);
		check(field(payload, "loop") == "0.25" && field(payload, "envelope") == "0.5" && field(payload, "stage") == "3",
		      "23. the client reads loop, envelope, and stage");
		check(field(payload, "video") == "\"clip.mov\"", "23. the client reads \"video\":\"clip.mov\"");
		check(field(payload, "gate") == "true" && field(payload, "cue") == "false" && field(payload, "erase") == "true",
		      "23. the client reads the gate, Cue, and Erase flags");
		params.setInt("source_kind", 0);
		publish(session, params);
		check(field(readText(client), "video") == "\"\"", "23. video is empty when the Source is not a Video");

		// SourceLoad stands in for the loader: the test plays App's part, choosing paths and resolving loads.
		const std::vector<std::string> videos = {"/v/a.mov", "/v/b.mov", "/v/c.mov"};
		SourceLoad load;
		bool isWebcam = true;
		std::string path;
		int index = -1;
		auto swapIn = [&] {
			if (!load.pollSwap(isWebcam, path, index)) return false;
			params.setInt("source_kind", isWebcam ? 3 : 1);
			params.setString("file_path", path);
			if (isWebcam) params.setInt("webcam_index", index);
			return true;
		};
		LoadRequest first = load.requestVideo(videos[0]);
		load.resolve(first.id, true, {});
		check(swapIn() && !isWebcam && path == videos[0] && load.committedPath() == videos[0] && !load.busy(),
		      "24. a resolved load swaps in on the next poll");
		check(!load.pollSwap(isWebcam, path, index), "24. a swap happens once");

		sendFrame(client.fd, 0x1, R"({"t":"next"})");
		sets = waitDrain(session);
		check(sets.size() == 1 && sets[0].kind == RemoteKind::Next, "24. next drains as one Next item");
		LoadRequest toNext = load.requestVideo(stepVideo(videos, params.getString("file_path"), +1));
		check(toNext.path == videos[1] && load.busy() && load.inflight() == toNext.id &&
		          load.committedPath() == videos[0],
		      "24. Next starts a load of b.mov and the committed Video stays a.mov");
		publish(session, params);
		check(field(readText(client), "video") == "\"a.mov\"", "24. the client still reads a.mov while loading");

		sendFrame(client.fd, 0x1, R"({"t":"prev"})");
		sets = waitDrain(session);
		check(sets.size() == 1 && sets[0].kind == RemoteKind::Prev, "24. prev drains as one Prev item");
		LoadRequest toPrev = load.requestVideo(stepVideo(videos, params.getString("file_path"), -1));
		check(toPrev.path == videos[2] && toPrev.id != toNext.id && load.inflight() == toPrev.id,
		      "24. Prev starts a load of c.mov that replaces the Next load");
		load.resolve(toNext.id, true, {});
		check(!swapIn() && load.busy() && load.committedPath() == videos[0],
		      "24. resolving the cancelled load swaps nothing");
		load.resolve(toPrev.id, true, {});
		check(swapIn() && path == videos[2] && load.committedPath() == videos[2] && !load.busy(),
		      "24. resolving the latest load swaps c.mov in one step");
		publish(session, params);
		check(field(readText(client), "video") == "\"c.mov\"", "24. the client reads c.mov after the swap");

		LoadRequest bad = load.requestVideo(videos[1]);
		load.resolve(bad.id, false, "could not open");
		check(!swapIn() && load.committedPath() == videos[2], "25. a failed open keeps the committed Video");
		std::string failure = load.takeFailure();
		check(failure == "could not open" && load.takeFailure().empty() && !load.busy(),
		      "25. the failure reason comes out once");
		session.send(rejectFrame("source", failure));
		payload = readText(client);
		check(field(payload, "name") == "\"source\"" && field(payload, "reason") == "\"could not open\"",
		      "25. the client reads \"reason\":\"could not open\"");

		params.setInt("webcam_index", 2);
		sendFrame(client.fd, 0x1, R"({"t":"webcam"})");
		sets = waitDrain(session);
		check(sets.size() == 1 && sets[0].kind == RemoteKind::Webcam, "26. webcam drains as one Webcam item");
		LoadRequest cam = load.requestWebcam(params.getInt("webcam_index"));
		check(cam.webcam && cam.webcamIndex == 2 && load.busy() && !load.committedWebcam(),
		      "26. a webcam load leaves the committed Source on the Video");
		LoadRequest cam4 = load.requestWebcam(4);
		load.resolve(cam.id, true, {});
		check(!swapIn() && !load.committedWebcam(), "26. a second webcam request cancels the first");
		load.resolve(cam4.id, true, {});
		check(swapIn() && isWebcam && index == 4 && load.committedWebcam() && load.committedWebcamIndex() == 4 &&
		          load.committedPath() == videos[2],
		      "26. resolving the latest webcam load swaps in webcam 4");

		params.setInt("source_kind", 1);
		sendFrame(client.fd, 0x1, setFrame("source_kind", "3"));
		sets = waitDrain(session);
		ApplyResult result = sets.size() == 1 ? applyRemoteSet(params, sets[0]) : ApplyResult{true, {}, false, 0.0f};
		check(!result.reject && result.startWebcam && params.getInt("source_kind") == 1,
		      "26. a set of source_kind 3 asks for a webcam load and leaves the param alone");

		// App's handling of a Load: look the name up in the folder, reject an unknown one, otherwise start a load.
		auto onLoad = [&](const RemoteSet& set) {
			std::string hit = videoByName(videos, set.name);
			if (hit.empty()) {
				session.send(rejectFrame("source", "no such video"));
				return LoadRequest{0, false, {}, 0};
			}
			return load.requestVideo(hit);
		};
		const std::string committed = load.committedPath();
		sendFrame(client.fd, 0x1, R"({"t":"load","name":"b.mov"})");
		sets = waitDrain(session);
		check(sets.size() == 1 && sets[0].kind == RemoteKind::Load && sets[0].name == "b.mov",
		      "29. load drains as one Load item naming b.mov");
		LoadRequest toB = sets.size() == 1 ? onLoad(sets[0]) : LoadRequest{0, false, {}, 0};
		check(toB.path == videos[1] && load.inflight() == toB.id && load.committedPath() == committed &&
		          load.committedWebcam() && !params.getBool("commit_active"),
		      "29. a tap starts a load of b.mov, keeps the committed Source, and does not start a Commit");
		sendFrame(client.fd, 0x1, R"({"t":"load","name":"a.mov"})");
		sets = waitDrain(session);
		LoadRequest toA = sets.size() == 1 ? onLoad(sets[0]) : LoadRequest{0, false, {}, 0};
		check(toA.path == videos[0] && load.inflight() == toA.id, "29. a second tap starts a load of a.mov");
		load.resolve(toB.id, true, {});
		check(!swapIn() && load.busy() && load.committedPath() == committed,
		      "29. resolving the first tap swaps nothing");
		load.resolve(toA.id, true, {});
		check(swapIn() && !isWebcam && path == videos[0] && load.committedPath() == videos[0] &&
		          !params.getBool("commit_active"),
		      "29. resolving the second tap swaps a.mov in");

		sendFrame(client.fd, 0x1, R"({"t":"load","name":"zzz.mov"})");
		sets = waitDrain(session);
		LoadRequest none = sets.size() == 1 ? onLoad(sets[0]) : LoadRequest{1, false, {}, 0};
		payload = readText(client);
		check(none.id == 0 && field(payload, "name") == "\"source\"" &&
		          field(payload, "reason") == "\"no such video\"" && !load.busy() && load.committedPath() == videos[0],
		      "30. a load of an unknown name is rejected as no such video and keeps the committed Video");

		const std::string jpeg("\xff\xd8\0\r\n\r\nthumb\xff\xd9", 14);
		session.setThumbs({{"a.mov", jpeg}, {"my clip.mov", "x"}});
		std::string res = httpGet(port, "/thumb?name=a.mov&token=" + session.token());
		size_t bodyAt = res.find("\r\n\r\n");
		check(res.rfind("HTTP/1.1 200", 0) == 0 && res.find("Content-Type: image/jpeg") != std::string::npos &&
		          bodyAt != std::string::npos && res.substr(bodyAt + 4) == jpeg,
		      "31. GET /thumb returns the registered bytes as image/jpeg");
		check(httpGet(port, "/thumb?name=a.mov").rfind("HTTP/1.1 403", 0) == 0,
		      "31. GET /thumb without a token returns 403");
		check(httpGet(port, "/thumb?name=b.mov&token=" + session.token()).rfind("HTTP/1.1 404", 0) == 0,
		      "31. GET /thumb for an unknown name returns 404");
		res = httpGet(port, "/thumb?name=my%20clip.mov&token=" + session.token());
		check(res.rfind("HTTP/1.1 200", 0) == 0 && res.substr(res.find("\r\n\r\n") + 4) == "x",
		      "31. a percent-encoded name finds its thumbnail");

		sendFrame(client.fd, 0x1, R"({"t":"dub","name":"b.mov"})");
		sets = waitDrain(session);
		check(sets.size() == 1 && sets[0].kind == RemoteKind::Dub && sets[0].name == "b.mov",
		      "43. dub drains as one Dub item naming b.mov");
		sendFrame(client.fd, 0x1, R"({"t":"page","names":["a.mov","b.mov"]})");
		sets = waitDrain(session);
		check(sets.size() == 1 && sets[0].kind == RemoteKind::Page && sets[0].names.size() == 2 &&
		          sets[0].names[0] == "a.mov" && sets[0].names[1] == "b.mov",
		      "44. page drains as one Page item naming the Videos on it");
		sendFrame(client.fd, 0x1, R"({"t":"page","names":[]})");
		sets = waitDrain(session);
		check(sets.size() == 1 && sets[0].kind == RemoteKind::Page && sets[0].names.empty(),
		      "44. an empty page drains as a Page with no Videos");

		close(client.fd);
	}

	{
		ParamBus params;
		std::string snapshot = remoteSnapshot(params, {}, {"a.mov", "b.mov"});
		check(snapshot.find(R"("videos":["a.mov","b.mov"])") != std::string::npos,
		      "32. the snapshot lists the folder's Videos by filename");
		check(remoteSnapshot(params).find(R"("videos":[])") != std::string::npos,
		      "32. with no Videos the snapshot lists none");
	}
	const std::vector<std::string> folder = {"/v/a.mov", "/v/b.mov"};
	check(videoByName(folder, "b.mov") == "/v/b.mov", "33. videoByName finds a path by filename");
	check(videoByName(folder, "c.mov").empty() && videoByName(folder, "v/b.mov").empty(),
	      "33. videoByName returns empty for an unknown name");
	check(videosByName(folder, {"b.mov", "nope", "a.mov"}) == std::vector<std::string>{"/v/b.mov", "/v/a.mov"},
	      "33. videosByName keeps the names that are in the folder, in order");
	check(!commitReachedEnd(true, false, true) && !commitReachedEnd(false, true, true) &&
	          !commitReachedEnd(false, false, false) && commitReachedEnd(false, false, true),
	      "45. a Commit ends only when the Video produces no frame, is not looping, and the reader has ended");

	check(stepVideo({}, "/v/a.mov", 1).empty() && stepVideo({}, "", -1).empty(), "27. an empty list steps to nothing");
	const std::vector<std::string> abc = {"a", "b", "c"};
	check(!stillSourceCancelsLoad(0, 0) && !stillSourceCancelsLoad(2, 2),
	      "34. staying on Shader or Black does not cancel a load");
	check(stillSourceCancelsLoad(0, 1) && stillSourceCancelsLoad(2, 3), "34. choosing Shader or Black cancels a load");
	check(!stillSourceCancelsLoad(1, 0) && !stillSourceCancelsLoad(3, 0),
	      "34. choosing a Video or the webcam does not cancel a load");
	check(stepVideo(abc, "a", 1) == "b" && stepVideo(abc, "b", -1) == "a", "27. next and prev step one Video");
	check(stepVideo(abc, "c", 1) == "a" && stepVideo(abc, "a", -1) == "c", "27. stepping wraps at both ends");
	{
		SourceLoad dropped;
		LoadRequest stale = dropped.requestVideo("a.mov");
		dropped.resolve(stale.id, true, {});
		dropped.cancel();
		bool webcam = false;
		std::string path;
		int index = 0;
		check(!dropped.pollSwap(webcam, path, index) && dropped.committedPath().empty(),
		      "28. cancel drops a ready load before it swaps");
		dropped.resolve(stale.id, true, {});
		check(!dropped.pollSwap(webcam, path, index), "28. a resolve after cancel does not swap");
	}
	check(stepVideo(abc, "x", -1) == "c" && stepVideo(abc, "x", 1) == "a",
	      "27. from an unknown Video prev picks the last and next the first");
	{
		const auto a = dir / "set-a", b = dir / "set-b";
		std::filesystem::create_directories(a / "sub.mov");
		std::filesystem::create_directories(b);
		for (const char* name : {"b.mov", "A.MP4", "notes.txt", "c.mkv"})
			std::ofstream{a / name};
		for (const char* name : {"z.m4v", "y.avi"})
			std::ofstream{b / name};
		const std::vector<std::string> aVideos = {(a / "A.MP4").string(), (a / "b.mov").string(),
		                                          (a / "c.mkv").string()};
		const std::vector<std::string> bVideos = {(b / "y.avi").string(), (b / "z.m4v").string()};
		check(scanVideos(a.string()) == aVideos && scanVideos(b.string()) == bVideos,
		      "35. scanVideos returns only the Videos, sorted by filename");

		FolderList folders;
		folders.add({a.string(), scanVideos(a.string())});
		folders.add({b.string(), scanVideos(b.string())});
		check(folders.paths() == std::vector<std::string>{a.string(), b.string()} && !folders.current(),
		      "35. both folders are listed in order and none is current yet");
		std::ofstream{a / "late.mov"};
		check(folders.select(a.string()) && folders.current()->videos == aVideos,
		      "36. selecting a folder gives the Videos captured at add, without a Video added later");

		RemoteSession session(tokenFile, "127.0.0.1", 0);
		session.setEnabled(true);
		Ws client = openClient(session.port(), session.token());
		ParamBus params;
		params.setString("file_path", "/tmp/videos/clip.mov");
		params.setInt("source_kind", 1);
		params.setString("videos_folder", a.string());
		// Stands in for SourceController::videos(). App's handling of a Folder: show a listed folder's stored
		// Videos the way SourceController::showFolder does, or reject an unknown path.
		std::vector<std::string> shown = aVideos;
		auto onFolder = [&](const RemoteSet& set) {
			if (!folders.select(set.name)) {
				session.send(rejectFrame("folder", "no such folder"));
				return;
			}
			shown = folders.current()->videos;
			params.setString("videos_folder", set.name);
			params.setInt("videos_count", int(shown.size()));
		};
		sendFrame(client.fd, 0x1, R"({"t":"folder","name":")" + b.string() + "\"}");
		std::vector<RemoteSet> sets = waitDrain(session);
		check(sets.size() == 1 && sets[0].kind == RemoteKind::Folder && sets[0].name == b.string(),
		      "37. folder drains as one Folder item naming the second path");
		if (sets.size() == 1) onFolder(sets[0]);
		check(shown == bVideos && params.getString("videos_folder") == b.string() &&
		          params.getString("file_path") == "/tmp/videos/clip.mov" && params.getInt("source_kind") == 1,
		      "37. the switch shows the second folder's Videos and keeps the Source");

		std::vector<std::string> names;
		for (const std::string& p : shown)
			names.push_back(std::filesystem::path(p).filename());
		session.send(remoteSnapshot(params, {}, names, folders.paths()));
		std::string payload = readText(client);
		check(payload.find("\"folders\":[\"" + a.string() + "\",\"" + b.string() + "\"]") != std::string::npos,
		      "38. the snapshot lists both folders by full path");
		check(payload.find(R"("videos":["y.avi","z.m4v"])") != std::string::npos &&
		          field(payload, "videos_folder") == "\"" + b.string() + "\"",
		      "38. the snapshot's Videos and Videos folder are the second folder's");

		sendFrame(client.fd, 0x1, R"({"t":"folder","name":"/nowhere"})");
		sets = waitDrain(session);
		if (sets.size() == 1) onFolder(sets[0]);
		payload = readText(client);
		check(sets.size() == 1 && field(payload, "name") == "\"folder\"" &&
		          field(payload, "reason") == "\"no such folder\"" && params.getString("videos_folder") == b.string() &&
		          folders.current()->path == b.string() && shown == bVideos,
		      "39. an unknown folder is rejected as no such folder and the Videos folder stays");
		close(client.fd);
	}
	{
		ParamBus params;
		std::string hot = remoteSnapshot(params, {}, {"a.mov"}, {"/v"}, false);
		check(hot.find("\"videos\"") == std::string::npos && hot.find("\"folders\"") == std::string::npos &&
		          hot.rfind("{\"t\":\"snapshot\"", 0) == 0,
		      "40. a per-frame snapshot omits the Videos and the Folder list");
	}
	{
		RemoteSession session(tokenFile, "127.0.0.1", 0);
		session.setEnabled(true);
		session.setSilenceWindow(300);
		Ws client = openClient(session.port(), session.token());
		timeval sendTimeout{0, 200000};
		setsockopt(client.fd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof sendTimeout);
		std::atomic<bool> run{true};
		std::thread holder([&] {
			while (run) {
				sendFrame(client.fd, 0x1, R"({"t":"hold","name":"gate"})");
				std::this_thread::sleep_for(std::chrono::milliseconds(40));
			}
		});
		check(waitPads(session, true, false), "41. the gate pad opens before the snapshot flood");
		ParamBus params;
		std::vector<std::string> many;
		for (int i = 0; i < 102; ++i)
			many.push_back("openvid-1m_clip_" + std::to_string(i) + "_0123456789abcdef.mp4");
		int off = 0, samples = 0, frame = 0;
		auto start = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
			RemoteLive live;
			live.loop = frame++ / 200.f;
			session.send(remoteSnapshot(params, live, many));
			if (std::chrono::steady_clock::now() - start > std::chrono::seconds(1)) {
				if (!session.pads().gate) ++off;
				++samples;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}
		check(samples > 10 && off == 0, "41. a folder of Videos does not drop the gate while the page is slow to read");
		run = false;
		holder.join();
		close(client.fd);
	}
	{
		RemoteSession session(tokenFile, "127.0.0.1", 0);
		session.setEnabled(true);
		uint16_t port = session.port();
		const std::string token = session.token();
		check(session.tokenRequired() && session.url().find("?token=" + token) != std::string::npos,
		      "42. the URL carries the token while it is required");

		session.setTokenRequired(false);
		check(!session.tokenRequired() && session.url().find("token=") == std::string::npos,
		      "42. the URL drops the token when it is off");
		std::string res = httpGet(port, "/");
		check(res.rfind("HTTP/1.1 200", 0) == 0 && res.find("id=\"two-hands\"") != std::string::npos,
		      "42. GET / without a token returns the page when the token is off");
		check(httpGet(port, "/?token=" + token).rfind("HTTP/1.1 200", 0) == 0,
		      "42. GET / with the token still returns the page when the token is off");
		int fd = connectTo(port);
		std::string req = upgradeRequest("/ws");
		send(fd, req.data(), req.size(), 0);
		Ws ws{fd, {}};
		std::string head, payload;
		int opcode = 0;
		bool got = readHead(ws, head) && readFrame(ws, opcode, payload);
		check(got && head.rfind("HTTP/1.1 101", 0) == 0,
		      "42. a WebSocket upgrade without a token returns 101 when the token is off");
		close(fd);
		check(httpGet(port, "/thumb?name=missing").rfind("HTTP/1.1 404", 0) == 0,
		      "42. GET /thumb without a token is no longer refused when the token is off");

		session.setTokenRequired(true);
		check(session.tokenRequired() && httpGet(port, "/").rfind("HTTP/1.1 403", 0) == 0,
		      "42. GET / without a token returns 403 when the token is back on");
		check(session.url().find("?token=" + token) != std::string::npos, "42. the URL carries the token again");
	}
	{
		auto log = dir / "web-use.jsonl";
		RemoteSession session(tokenFile, "127.0.0.1", 0);
		session.setUsageFile(log);
		session.setEnabled(true);
		Ws client = openClient(session.port(), session.token());
		sendFrame(client.fd, 0x1, setFrame("fx_mode", "5"));
		sendFrame(client.fd, 0x1, "{\"t\":\"use\",\"e\":\"set\",\"name\":\"fx_mode\",\"v\":5}");
		sendFrame(client.fd, 0x1, "{\"t\":\"use\",\"e\":\"act\",\"name\":\"dub\",\"v\":\"clip.mp4\"}");
		sendFrame(client.fd, 0x1, "{\"t\":\"use\",\"e\":\"hold\",\"name\":\"gate\"}");
		sendFrame(client.fd, 0x1, "{\"t\":\"use\",\"e\":\"nope\",\"name\":\"x\"}");
		sendFrame(client.fd, 0x1, "{\"t\":\"use\",\"name\":\"fx_mode\"}");
		std::vector<RemoteSet> sets = waitDrain(session);
		check(sets.size() == 1 && sets[0].kind == RemoteKind::Set && sets[0].name == "fx_mode",
		      "43. a use does not enter the control queue");
		std::string data;
		for (int i = 0; i < 100; ++i) {
			std::ifstream in(log);
			data.assign(std::istreambuf_iterator<char>(in), {});
			if (data.find("\"e\":\"hold\"") != std::string::npos && data.find("clip.mp4") != std::string::npos) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		check(data.find("\"e\":\"set\"") != std::string::npos &&
		          data.find("\"name\":\"fx_mode\"") != std::string::npos && data.find("\"v\":5") != std::string::npos,
		      "43. a use gesture is appended to the usage file");
		check(data.find("\"v\":\"clip.mp4\"") != std::string::npos, "43. a dub name is kept on the gesture");
		int fxLines = 0;
		for (size_t at = 0; (at = data.find("fx_mode", at)) != std::string::npos; at += 7)
			++fxLines;
		check(fxLines == 1, "43. a control set is not logged unless the page sends use");
		check(data.find("nope") == std::string::npos, "43. an unknown gesture is not stored");
		close(client.fd);
	}
	std::filesystem::remove_all(dir);
	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
