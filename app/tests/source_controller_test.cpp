#include "io/SourceController.hpp"
#include "io/VideoSource.hpp"
#include "ui/ParamBus.hpp"

#include <algorithm>
#include <chrono>
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

// What the fake decoders share: which Videos fail, block, or run out, and what was opened and stopped.
struct World {
	std::mutex mu;
	std::condition_variable cv;
	std::set<std::string> broken;
	std::set<std::string> held;
	std::map<std::string, int> frames;
	std::set<int> brokenCameras;
	std::vector<std::string> opened;
	std::vector<std::string> stopped;
	std::vector<std::string> restarted;

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
};

// A Video whose first frame is 16x9. It has frames[path] further frames, or never runs out when unlisted.
struct FakeVideo : VideoDecoder {
	explicit FakeVideo(World& w) : world(w) {}
	World& world;
	std::string path;
	bool open = false;
	bool loop = true;
	bool paused = false;
	bool isEnded = false;
	int left = 0;

	bool startFile(const std::string& p) override {
		std::unique_lock<std::mutex> lock(world.mu);
		world.cv.wait(lock, [&] { return !world.held.count(p); });
		if (world.broken.count(p)) return false;
		path = p;
		open = true;
		isEnded = false;
		left = world.frames.count(p) ? world.frames[p] : 1 << 30;
		world.opened.push_back(p);
		return true;
	}
	void setPaused(bool p) override { paused = p; }
	void setLoop(bool l) override { loop = l; }
	void setLoopRange(float, float) override {}
	void restartAtLoopStart() override {
		std::lock_guard<std::mutex> lock(world.mu);
		world.restarted.push_back(path);
	}
	void tick(double) override {}
	bool grab(unsigned int, int& w, int& h) override {
		if (!open || paused) return false;
		if (left <= 0) {
			if (!loop) isEnded = true;
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
		open = !world.brokenCameras.count(index);
		return open;
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
	int uploads = 0;
	int width = 0;
	int height = 0;
	unsigned int pictureTexture() override { return 1; }
	void uploadPicture(const std::vector<unsigned char>&, int w, int h) override {
		++uploads;
		width = w;
		height = h;
	}
};

static DecoderFactory fakes(World& world) {
	return {[&world] { return std::make_unique<FakeVideo>(world); },
	        [&world] { return std::make_unique<FakeCamera>(world); }};
}

// Ticks like the frame loop until done says so. False when it never does.
static bool tickUntil(SourceController& source, ParamBus& params, const std::function<bool()>& done) {
	for (int i = 0; i < 600; ++i) {
		source.tick(1.0 / 60.0, params);
		if (done()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return false;
}

static bool waitFor(const std::function<bool()>& done) {
	for (int i = 0; i < 600; ++i) {
		if (done()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return false;
}

// Ticks for a while and reports whether a load ever swapped in.
static bool swapsWithin(SourceController& source, ParamBus& params, int ticks) {
	bool swapped = false;
	for (int i = 0; i < ticks; ++i) {
		source.tick(1.0 / 60.0, params);
		swapped = swapped || source.loadSwapped();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return swapped;
}

int main() {
	{
		World world;
		FakePicture picture;
		ParamBus params;
		SourceController source(picture, fakes(world));
		auto swapped = [&] { return source.loadSwapped(); };

		world.hold("/v/a.mov");
		source.loadVideo("/v/a.mov", 0, 1);
		source.tick(1.0 / 60.0, params);
		check(!source.loadSwapped() && params.getInt("source_kind") == 0 && picture.uploads == 0,
		      "1. while a Video opens, the live Source stays");
		world.release("/v/a.mov");
		check(tickUntil(source, params, swapped) && params.getInt("source_kind") == 1 &&
		          params.getString("file_path") == "/v/a.mov",
		      "1. once it has a first frame it becomes the Source in one step");
		check(picture.uploads == 1 && picture.width == 16 && source.frameWidth() == 16 && source.frameHeight() == 9,
		      "1. its first frame is the picture on that tick");
		source.tick(1.0 / 60.0, params);
		check(!source.loadSwapped(), "1. the swap is reported for one tick");

		source.loadVideo("/v/b.mov", 0, 1);
		check(tickUntil(source, params, swapped) && params.getString("file_path") == "/v/b.mov" &&
		          waitFor([&] { return world.count(&World::stopped, "/v/a.mov") == 1; }),
		      "2. the next Video replaces it and the old one is stopped");

		world.hold("/v/c.mov");
		source.loadVideo("/v/c.mov", 0, 1);
		source.tick(1.0 / 60.0, params);
		source.loadVideo("/v/d.mov", 0, 1);
		source.tick(1.0 / 60.0, params);
		world.release("/v/c.mov");
		check(tickUntil(source, params, swapped) && params.getString("file_path") == "/v/d.mov" &&
		          !swapsWithin(source, params, 20) && params.getString("file_path") == "/v/d.mov",
		      "3. a newer request wins over one still opening");

		world.broken.insert("/v/bad.mov");
		source.loadVideo("/v/bad.mov", 0, 1);
		std::string failure;
		check(tickUntil(source, params, [&] { return !(failure = source.takeLoadFailure()).empty(); }) &&
		          failure == "could not open bad.mov",
		      "4. a Video that cannot open reports why");
		check(source.takeLoadFailure().empty() && params.getString("file_path") == "/v/d.mov" &&
		          params.getInt("source_kind") == 1,
		      "4. the reason comes out once and the live Source stays");

		source.restartFileAtLoopStart();
		check(world.count(&World::restarted, "/v/d.mov") == 1, "5. a restart reaches the live Video");
	}

	{
		World world;
		FakePicture picture;
		ParamBus params;
		SourceController source(picture, fakes(world));
		auto swapped = [&] { return source.loadSwapped(); };
		params.setBool("file_loop", true);
		params.setBool("file_paused", true);

		world.frames["/v/short.mov"] = 3;
		world.hold("/v/short.mov");
		source.queueDub("/v/short.mov", 0, 1);
		source.tick(1.0 / 60.0, params);
		check(!source.committing() && params.getBool("file_loop"),
		      "6. a Dub does not Commit before its Video is the Source");
		world.release("/v/short.mov");
		check(tickUntil(source, params, swapped) && source.committing() && source.commitVideo() == "/v/short.mov",
		      "6. the Commit starts on the tick the Video swaps in");
		check(!params.getBool("file_loop") && !params.getBool("file_paused"),
		      "6. a Commit plays the Video once, unpaused");
		check(tickUntil(source, params, [&] { return !source.committing(); }) && params.getBool("file_loop") &&
		          params.getString("file_path") == "/v/short.mov" && source.commitVideo().empty(),
		      "6. the Commit ends when the Video runs out, and looping comes back");

		world.broken.insert("/v/bad.mov");
		source.queueDub("/v/bad.mov", 0, 1);
		check(tickUntil(source, params, [&] { return !source.takeLoadFailure().empty(); }),
		      "7. a Dub of a broken Video fails");
		source.loadVideo("/v/a.mov", 0, 1);
		check(tickUntil(source, params, swapped) && !source.committing() && !swapsWithin(source, params, 5) &&
		          !source.committing(),
		      "7. the failed Dub does not Commit the next Video that loads");

		source.queueDub("/v/long.mov", 0, 1);
		check(tickUntil(source, params, swapped) && source.committing() && !params.getBool("file_loop"),
		      "8. a Dub of a long Video is committing");
		source.loadVideo("/v/a.mov", 0, 1);
		source.tick(1.0 / 60.0, params);
		bool stillCommitting = source.committing();
		check(tickUntil(source, params, swapped) && stillCommitting && !source.committing() &&
		          params.getBool("file_loop") && params.getString("file_path") == "/v/a.mov",
		      "8. loading another Video ends the Commit when it swaps in, and looping comes back");

		source.queueDub("/v/long.mov", 0, 1);
		tickUntil(source, params, swapped);
		source.queueDub("/v/other.mov", 0, 1);
		check(tickUntil(source, params, swapped) && source.committing() && source.commitVideo() == "/v/other.mov",
		      "9. a second Dub takes the Commit over");
		params.setInt("source_kind", 2);
		source.tick(1.0 / 60.0, params);
		source.tick(1.0 / 60.0, params);
		check(!source.committing() && params.getBool("file_loop"),
		      "9. choosing black ends the Commit, and looping is what it was before the first Dub");
	}

	{
		World world;
		FakePicture picture;
		ParamBus params;
		SourceController source(picture, fakes(world));

		source.primePage({"/v/p.mov", "/v/q.mov"}, 0, 1);
		check(waitFor([&] {
				  return world.count(&World::opened, "/v/p.mov") == 1 && world.count(&World::opened, "/v/q.mov") == 1;
			  }),
		      "10. a page opens its Videos ahead of a tap");
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		// From here a load of p would block, so a swap on the very next tick can only be the one opened ahead.
		world.hold("/v/p.mov");
		source.queueDub("/v/p.mov", 0, 1);
		source.tick(1.0 / 60.0, params);
		check(source.loadSwapped() && params.getString("file_path") == "/v/p.mov" &&
		          params.getInt("source_kind") == 1 && picture.uploads == 1,
		      "10. a Video opened ahead becomes the Source on the tick it is asked for");
		check(source.committing() && source.commitVideo() == "/v/p.mov", "10. and its Dub Commits on that same tick");
		world.release("/v/p.mov");

		world.hold("/v/q.mov");
		source.loadVideo("/v/q.mov", 0.5f, 0.25f);
		source.tick(1.0 / 60.0, params);
		check(!source.loadSwapped(), "11. a Video opened ahead at another loop range is loaded afresh");
		world.release("/v/q.mov");
		check(tickUntil(source, params, [&] { return source.loadSwapped(); }) &&
		          params.getString("file_path") == "/v/q.mov" && !source.committing(),
		      "11. and swaps in when that load is ready, ending the Commit");
	}

	{
		World world;
		FakePicture picture;
		ParamBus params;
		SourceController source(picture, fakes(world));
		auto swapped = [&] { return source.loadSwapped(); };

		source.loadVideo("/v/a.mov", 0, 1);
		check(tickUntil(source, params, swapped) && params.getString("file_path") == "/v/a.mov",
		      "12. a Video asked for before the first tick still loads");
		world.hold("/v/slow.mov");
		source.queueDub("/v/slow.mov", 0, 1);
		source.tick(1.0 / 60.0, params);
		params.setInt("source_kind", 2);
		source.tick(1.0 / 60.0, params);
		world.release("/v/slow.mov");
		check(!swapsWithin(source, params, 30) && params.getInt("source_kind") == 2 && !source.committing(),
		      "12. choosing black drops a Video that is still opening, and its Dub");
		check(waitFor([&] { return world.count(&World::stopped, "/v/a.mov") == 1; }),
		      "12. and stops the Video that was live");

		world.hold("/v/slow.mov");
		source.loadVideo("/v/slow.mov", 0, 1);
		source.tick(1.0 / 60.0, params);
		source.tick(1.0 / 60.0, params);
		world.release("/v/slow.mov");
		check(tickUntil(source, params, swapped) && params.getInt("source_kind") == 1,
		      "13. staying on black does not drop a load asked for afterwards");

		params.setInt("source_kind", 0);
		source.tick(1.0 / 60.0, params);
		params.setString("file_path", "/v/saved.mov");
		params.setInt("source_kind", 1);
		check(tickUntil(source, params, swapped) && params.getString("file_path") == "/v/saved.mov",
		      "14. switching the Source kind to Video loads the Video the controls name");
	}

	{
		World world;
		FakePicture picture;
		ParamBus params;
		SourceController source(picture, fakes(world));
		auto swapped = [&] { return source.loadSwapped(); };

		source.loadWebcam(2);
		check(tickUntil(source, params, swapped) && params.getInt("source_kind") == 3 &&
		          params.getInt("webcam_index") == 2 && picture.width == 8,
		      "15. a webcam becomes the Source with its index");
		source.tick(1.0 / 60.0, params);
		check(source.frameWidth() == 8 && source.frameHeight() == 6, "15. its frames are the picture");
		params.setInt("webcam_index", 1);
		check(tickUntil(source, params, swapped) && params.getInt("webcam_index") == 1,
		      "15. changing the index while it is live loads that webcam");
		world.brokenCameras.insert(5);
		source.loadWebcam(5);
		std::string failure;
		check(tickUntil(source, params, [&] { return !(failure = source.takeLoadFailure()).empty(); }) &&
		          failure == "could not open webcam" && params.getInt("webcam_index") == 1,
		      "15. a webcam that cannot open reports why and the live one stays");

		source.showFolder("/w", {"/w/x.mov", "/w/y.mov"}, params);
		source.tick(1.0 / 60.0, params);
		check(source.videos() == std::vector<std::string>{"/w/x.mov", "/w/y.mov"} &&
		          params.getString("videos_folder") == "/w" && params.getInt("videos_count") == 2,
		      "16. showing a folder lists its Videos without reading the disk");
	}

	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
