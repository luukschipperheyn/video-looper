#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "control/Instrument.hpp"
#include "io/PageCache.hpp"
#include "io/SourceLoad.hpp"
#include "io/VideoSource.hpp"
#include "ui/ParamBus.hpp"

// The life of the Source: a Video or webcam is asked for, opens off the main thread, becomes live in one step
// once it has a first frame, and a Video may then be Committed into the Loop until it ends.
//
// Every method runs on the main thread. tick is called once per frame; a request made between ticks takes
// effect on the next one. The live Source keeps playing until its replacement is ready. A failed load leaves
// the live Source alone and is reported once by takeLoadFailure. Choosing the shader, black, or Draw as the
// Source kind drops a load that has not swapped in.
//
// A Commit (queueDub) starts on the tick its Video becomes the Source: at once when the Video was opened ahead
// by primePage, otherwise when its load swaps in. While it runs, that Video plays once. file_loop and file_paused
// stay as the performer set them. It ends when the Video has no further frame, or when another Source is loaded.
class SourceController : public SourcePort {
public:
	// picture receives each decoded frame. decoders makes the Video and webcam decoders.
	SourceController(PicturePort& picture, DecoderFactory decoders);
	~SourceController() override;
	SourceController(const SourceController&) = delete;
	SourceController& operator=(const SourceController&) = delete;

	// Stops the loaders and the live Source. Safe to call twice.
	void shutdown();

	// Advances the live Source by dtSeconds and uploads its newest frame. True when a new picture was uploaded.
	bool tick(double dtSeconds, ParamBus& params);

	void loadVideo(const std::string& path, float loopStart, float loopLength) override;
	void queueDub(const std::string& path, float loopStart, float loopLength) override;
	void primePage(const std::vector<std::string>& paths, float loopStart, float loopLength) override;
	void loadWebcam(int index) override;
	void showFolder(const std::string& path, const std::vector<std::string>& videos, ParamBus& params) override;
	std::vector<std::string> videos() const override { return videoFiles; }

	// Restarts the live Video at the start of its loop range.
	void restartFileAtLoopStart();

	// Why the latest load failed, once. Empty if none. A failure also drops a Commit waiting on that load.
	std::string takeLoadFailure();
	// True on the tick a load replaced the live Source.
	bool loadSwapped() const { return swappedLoad; }
	// True while a Commit is writing a Video into the Loop. commitVideo is that Video's path.
	bool committing() const { return commitOn; }
	const std::string& commitVideo() const { return commitPath; }

	// Pixel size of the latest Video or webcam picture. 0 before the first one.
	int frameWidth() const { return frameW; }
	int frameHeight() const { return frameH; }
	float getFileNominalFps() const { return fileSrc->nominalFps(); }

private:
	bool swapInLoaded(ParamBus& params);
	void ensureSource(ParamBus& params);
	// Applies a queued load or Dub. True when a Video opened ahead was swapped in and its first frame shown.
	bool applyRequest(ParamBus& params);
	bool installReady(const std::string& path, float loopStart, float loopLength, ParamBus& params);
	void beginCommit(const std::string& path);
	void cancelCommit();
	void presentFrame(const std::vector<unsigned char>& bgra, int width, int height);
	void startLoad(const LoadRequest& request);
	void runLoads();
	std::string openSpare(const LoadRequest& request);
	void maybeRescanFolder(ParamBus& params);

	PicturePort& picture;
	DecoderFactory decoders;
	int frameW = 0;
	int frameH = 0;

	std::unique_ptr<CameraDecoder> webcam;
	bool webcamActive = false;
	std::unique_ptr<VideoDecoder> fileSrc;
	bool fileActive = false;
	int seenKind = -1;
	int seenWebcamIndex = -1;
	bool swappedLoad = false;

	// The worker owns the spares and spareFrame from a request until it resolves; after a swap they hold the
	// previous live sources until the worker stops them.
	SourceLoad load;
	std::unique_ptr<CameraDecoder> spareWebcam;
	std::unique_ptr<VideoDecoder> spareFile;
	std::vector<unsigned char> spareFrame;
	int spareW = 0;
	int spareH = 0;
	std::mutex jobMutex;
	std::condition_variable jobReady;
	LoadRequest job{0, false, {}, 0};
	bool retire = false;
	std::atomic<bool> quit{false};
	std::thread worker;

	std::vector<std::string> videoFiles;
	std::string lastVideosFolder;
	PageCache pages;
	struct PendingVideo {
		bool on = false;
		bool dub = false;
		std::string path;
		float loopStart = 0;
		float loopLength = 1;
	};
	PendingVideo pendingVideo;
	std::string dubOnSwap;
	bool commitOn = false;
	std::string commitPath;
};
