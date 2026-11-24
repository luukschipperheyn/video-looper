#include "control/Instrument.hpp"
#include "io/FolderList.hpp"
#include "io/SourceController.hpp"
#include "io/VideoSource.hpp"
#include "ui/ParamBus.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) ++failures;
}

struct World {
	std::mutex mu;
	std::condition_variable cv;
	std::set<std::string> held;
	std::map<std::string, int> frames;
	std::vector<std::string> opened;
	std::vector<std::string> stopped;
	std::vector<int> cameras;
	bool paused = false;
	float rangeStart = 0;
	float rangeEnd = 1;
	std::string playPath;

	void hold(const std::string& path) {
		std::lock_guard<std::mutex> lock(mu);
		held.insert(path);
	}
	void release(const std::string& path) {
		{
			std::lock_guard<std::mutex> lock(mu);
			held.erase(path);
		}
		cv.notify_all();
	}
	int count(const std::vector<std::string> World::* list, const std::string& path) {
		std::lock_guard<std::mutex> lock(mu);
		return int(std::count((this->*list).begin(), (this->*list).end(), path));
	}
	int cameraCount() {
		std::lock_guard<std::mutex> lock(mu);
		return int(cameras.size());
	}
	int lastCamera() {
		std::lock_guard<std::mutex> lock(mu);
		return cameras.empty() ? -1 : cameras.back();
	}
};

struct FakeVideo : VideoDecoder {
	explicit FakeVideo(World& w) : world(w) {}
	World& world;
	std::string path;
	bool open = false;
	bool loop = true;
	bool isPaused = false;
	bool isEnded = false;
	int left = 0;

	bool startFile(const std::string& p) override {
		std::unique_lock<std::mutex> lock(world.mu);
		world.cv.wait(lock, [&] { return !world.held.count(p); });
		path = p;
		open = true;
		isEnded = false;
		left = world.frames.count(p) ? world.frames[p] : 1 << 30;
		world.opened.push_back(p);
		return true;
	}
	void setPaused(bool p) override {
		isPaused = p;
		std::lock_guard<std::mutex> lock(world.mu);
		world.paused = p;
		world.playPath = path;
	}
	void setLoop(bool l) override { loop = l; }
	void setLoopRange(float start, float end) override {
		std::lock_guard<std::mutex> lock(world.mu);
		world.rangeStart = start;
		world.rangeEnd = end;
		world.playPath = path;
	}
	void restartAtLoopStart() override {}
	void tick(double) override {}
	bool grab(unsigned int, int& w, int& h) override {
		if (!open || isPaused || left <= 0) {
			if (open && !loop && left <= 0) isEnded = true;
			return false;
		}
		--left;
		w = 16;
		h = 9;
		return true;
	}
	bool copyFrame(std::vector<unsigned char>& bgra, int& w, int& h) override {
		if (!open) return false;
		w = 16;
		h = 9;
		bgra.assign(16 * 9 * 4, 7);
		return true;
	}
	bool ended() const override { return isEnded; }
	float nominalFps() const override { return 30.0f; }
	void stop() override {
		if (!open) return;
		open = false;
		std::lock_guard<std::mutex> lock(world.mu);
		world.stopped.push_back(path);
	}
};

struct FakeCamera : CameraDecoder {
	explicit FakeCamera(World& w) : world(w) {}
	World& world;
	bool open = false;
	bool start(int index) override {
		std::lock_guard<std::mutex> lock(world.mu);
		world.cameras.push_back(index);
		open = true;
		return true;
	}
	bool grab(unsigned int, int& w, int& h) override {
		w = 8;
		h = 6;
		return open;
	}
	bool copyFrame(std::vector<unsigned char>& bgra, int& w, int& h) override {
		if (!open) return false;
		w = 8;
		h = 6;
		bgra.assign(8 * 6 * 4, 3);
		return true;
	}
	void stop() override { open = false; }
};

struct FakePicture : PicturePort {
	unsigned int pictureTexture() override { return 1; }
	void uploadPicture(const std::vector<unsigned char>&, int, int) override {}
};

static DecoderFactory fakes(World& world) {
	return {[&world] { return std::make_unique<FakeVideo>(world); },
	        [&world] { return std::make_unique<FakeCamera>(world); }};
}

static void frame(SourceController& source, Instrument& instrument) {
	source.tick(1.0 / 60.0);
	instrument.record(source.status());
}

static bool framesUntil(SourceController& source, Instrument& instrument, const std::function<bool()>& done) {
	for (int i = 0; i < 600; ++i) {
		frame(source, instrument);
		if (done()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return false;
}

static bool near(float a, float b) {
	return std::fabs(a - b) < 1e-3f;
}

int main() {
	{
		World world;
		FakePicture picture;
		ParamBus params;
		SourceController source(picture, fakes(world));
		FolderList folders;
		Instrument instrument(params, source, folders, {});
		world.frames["/v/short.mov"] = 30;
		instrument.addFolder("/v", {"/v/short.mov"});
		instrument.setSwitch("file_loop", false);
		check(instrument.dubVideoNamed("short.mov").ok &&
		          framesUntil(source, instrument, [&] { return source.committing(); }) &&
		          source.commitVideo() == "/v/short.mov" && !params.getBool("file_loop"),
		      "1. a Dub Commits on the tick its Video swaps in, and file_loop stays off");
		instrument.setNumber("source_kind", int(SourceKind::Black));
		frame(source, instrument);
		check(!source.committing() && world.count(&World::stopped, "/v/short.mov") == 1 &&
		          !params.getBool("file_loop") && params.getInt("source_kind") == int(SourceKind::Black) &&
		          source.status().kind == SourceKind::Black && !source.status().swapped,
		      "1. choosing Black during a Dub ends the Commit, stops that Video, and file_loop stays");
	}

	{
		World world;
		FakePicture picture;
		ParamBus params;
		SourceController source(picture, fakes(world));
		FolderList folders;
		Instrument instrument(params, source, folders, {});
		world.hold("/v/opening.mov");
		instrument.setText("file_path", "/v/opening.mov");
		frame(source, instrument);
		check(!source.status().swapped && params.getString("file_path") == "/v/opening.mov",
		      "2. a Video that is still opening has not swapped");
		instrument.setNumber("source_kind", int(SourceKind::Draw));
		world.release("/v/opening.mov");
		bool swapped = false;
		for (int i = 0; i < 40; ++i) {
			frame(source, instrument);
			swapped = swapped || source.status().swapped;
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		check(!swapped && params.getInt("source_kind") == int(SourceKind::Draw) &&
		          source.status().kind == SourceKind::Draw,
		      "2. choosing Draw while a Video is opening drops that load and leaves Draw");
	}

	{
		World world;
		FakePicture picture;
		ParamBus params;
		SourceController source(picture, fakes(world));
		FolderList folders;
		Instrument instrument(params, source, folders, {});
		params.setInt("source_kind", int(SourceKind::File));
		params.setString("file_path", "/v/saved.mov");
		params.setFloat("file_loop_start", 0.2f);
		params.setFloat("file_loop_length", 0.4f);
		instrument.restoreSource();
		check(framesUntil(source, instrument,
		                  [&] { return source.status().swapped && source.status().path == "/v/saved.mov"; }) &&
		          params.getInt("source_kind") == int(SourceKind::File) &&
		          params.getString("file_path") == "/v/saved.mov" && world.count(&World::opened, "/v/saved.mov") == 1 &&
		          near(world.rangeStart, 0.2f) && near(world.rangeEnd, 0.6f),
		      "3. restoreSource loads the saved Video, and record leaves source_kind and file_path");

		instrument.setText("file_path", "/v/other.mov");
		check(framesUntil(source, instrument,
		                  [&] { return source.status().swapped && source.status().path == "/v/other.mov"; }) &&
		          params.getString("file_path") == "/v/other.mov" && params.getInt("source_kind") == int(SourceKind::File) &&
		          world.count(&World::opened, "/v/other.mov") == 1,
		      "4. setting file_path while a Video is playing reloads that Video");

		instrument.setSwitch("file_paused", true);
		instrument.setNumber("file_loop_start", 0.1f);
		instrument.setNumber("file_loop_length", 0.2f);
		frame(source, instrument);
		check(world.paused && world.playPath == "/v/other.mov" && near(world.rangeStart, 0.1f) &&
		          near(world.rangeEnd, 0.3f),
		      "5. file_paused and the loop range reach the decoder");
	}

	{
		World world;
		FakePicture picture;
		ParamBus params;
		SourceController source(picture, fakes(world));
		FolderList folders;
		Instrument instrument(params, source, folders, {});
		int before = world.cameraCount();
		instrument.setNumber("webcam_index", 4);
		for (int i = 0; i < 5; ++i) frame(source, instrument);
		check(world.cameraCount() == before && params.getInt("webcam_index") == 4 &&
		          source.status().kind != SourceKind::Webcam && params.getInt("source_kind") != int(SourceKind::Webcam),
		      "6. webcam_index while the Source is not a webcam does not load one");

		instrument.setNumber("source_kind", int(SourceKind::Webcam));
		check(framesUntil(source, instrument, [&] {
			      return source.status().swapped && source.status().kind == SourceKind::Webcam &&
			             source.status().webcamIndex == 4;
		      }) && params.getInt("source_kind") == int(SourceKind::Webcam) && params.getInt("webcam_index") == 4,
		      "6. choosing the webcam loads the current index");

		instrument.setNumber("webcam_index", 1);
		check(framesUntil(source, instrument, [&] {
			      return source.status().swapped && source.status().webcamIndex == 1;
		      }) && world.lastCamera() == 1 && params.getInt("webcam_index") == 1 &&
		          params.getInt("source_kind") == int(SourceKind::Webcam),
		      "6. webcam_index while a webcam is the Source loads that index");
	}

	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
