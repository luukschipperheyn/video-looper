#pragma once

#include "io/AVFFileSource.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Opens the Videos on one Web interface page ahead of a tap, and stops them off the render thread.
// setPage, take, and retire run on the main thread. The worker owns a source until take moves it out.
class PageCache {
public:
	PageCache();
	~PageCache();
	PageCache(const PageCache&) = delete;
	PageCache& operator=(const PageCache&) = delete;

	// paths are full paths, in tap order. A Video is opened at loopStart/loopLength. Passing the same page
	// again does nothing. An empty list drops what is open.
	void setPage(std::vector<std::string> paths, float loopStart, float loopLength);
	// Moves a ready Video and its first frame out. False when that Video is not open yet, or it was opened
	// at a different loop range.
	bool take(const std::string& path, float loopStart, float loopLength, AVFFileSource& out,
	          std::vector<unsigned char>& frame, int& w, int& h);
	// Stops src on the worker. src is left closed.
	void retire(AVFFileSource& src);
	void shutdown();

private:
	struct Slot {
		std::string path;
		std::unique_ptr<AVFFileSource> source;
		std::vector<unsigned char> frame;
		int w = 0;
		int h = 0;
		bool ready = false;
		bool failed = false;
	};

	void run();
	bool missing() const;
	std::string nextMissing() const;

	std::mutex mu;
	std::condition_variable cv;
	std::thread worker;
	std::vector<std::string> wanted;
	float loopStart = 0;
	float loopLength = 1;
	std::vector<Slot> slots;
	std::vector<std::unique_ptr<AVFFileSource>> retired;
	bool quit = false;
	bool stopped = false;
};
