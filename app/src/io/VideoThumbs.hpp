#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Makes one small JPEG per Video on its own thread, one at a time. The main thread hands it the folder's paths
// and takes the finished thumbnails as filename and bytes; a Video that fails to thumbnail is left out.
class VideoThumbs {
public:
	VideoThumbs();
	~VideoThumbs();
	VideoThumbs(const VideoThumbs&) = delete;
	VideoThumbs& operator=(const VideoThumbs&) = delete;

	// Drops the thumbnails made so far and starts on paths.
	void setVideos(std::vector<std::string> paths);
	// Every thumbnail made for the current paths, when that set changed since the last call.
	bool take(std::vector<std::pair<std::string, std::string>>& out);

private:
	void run();

	std::mutex mutex;
	std::condition_variable wake;
	std::vector<std::string> paths;
	uint64_t generation = 0;
	std::vector<std::pair<std::string, std::string>> made;
	bool changed = false;
	bool quit = false;
	std::thread worker;
};
