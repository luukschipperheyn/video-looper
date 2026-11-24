#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "control/Instrument.hpp"
#include "io/PageCache.hpp"
#include "io/VideoSource.hpp"

// The life of the Source: a Video or webcam is asked for, opens off the main thread, becomes live in one step
// once it has a first frame, and a Video may then be Committed into the Loop until it ends.
//
// Every method runs on the main thread. tick is called once per frame; a request made between ticks takes
// effect on the next one. The live Source keeps playing until its replacement is ready. A failed load leaves
// the live Source alone and is reported once by takeLoadFailure. The tick that first sees the failure drops a
// Dub waiting on that load. choose drops a load that has not swapped in.
//
// A Commit (queueDub) starts on the tick its Video becomes the Source: at once when the Video was opened ahead
// by primePage, otherwise when its load swaps in. While it runs, that Video plays once. Playback stored with
// setPlayback is left alone. It ends when the Video has no further frame, or when another Source is loaded.
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
	bool tick(double dtSeconds);

	void loadVideo(const std::string& path, float loopStart, float loopLength) override;
	void queueDub(const std::string& path, float loopStart, float loopLength) override;
	void primePage(const std::vector<std::string>& paths, float loopStart, float loopLength) override;
	void loadWebcam(int index) override;
	void choose(SourceKind kind) override;
	void setPlayback(bool loop, bool paused, float loopStart, float loopLength) override;
	SourceStatus status() const override;

	void restartFileAtLoopStart() override;

	// Why the latest load failed, once. Empty if none.
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
	struct LoadRequest {
		enum class What { Video, Webcam };
		uint64_t id = 0;
		What what = What::Video;
		std::string path;
		int webcamIndex = 0;
		float loopStart = 0;
		float loopLength = 1;
	};
	enum class LoadPhase { Idle, Loading, Ready, Failed };

	bool swapInLoaded();
	bool applyRequest();
	bool installReady(const std::string& path, float loopStart, float loopLength);
	void beginCommit(const std::string& path);
	void cancelCommit();
	void presentFrame(const std::vector<unsigned char>& bgra, int width, int height);
	void startLoad(const LoadRequest& request);
	void runLoads();
	std::string openSpare(const LoadRequest& request);

	LoadRequest requestVideoLoad(std::string path, float loopStart, float loopLength);
	LoadRequest requestWebcamLoad(int index);
	LoadRequest requestLoad(LoadRequest next);
	void cancelLoad();
	uint64_t loadInflight() const;
	void resolveLoad(uint64_t id, bool ok, std::string reason);
	bool pollSwap(bool& webcam, std::string& path, int& webcamIndex);
	bool takeDropDub();
	std::string takeFailure();

	PicturePort& picture;
	DecoderFactory decoders;
	int frameW = 0;
	int frameH = 0;

	std::unique_ptr<CameraDecoder> webcam;
	bool webcamActive = false;
	std::unique_ptr<VideoDecoder> fileSrc;
	bool fileActive = false;
	SourceKind reportedKind = SourceKind::Shader;
	bool swappedLoad = false;
	bool playLoop = true;
	bool playPaused = false;
	float playStart = 0;
	float playLength = 1;

	std::unique_ptr<CameraDecoder> spareWebcam;
	std::unique_ptr<VideoDecoder> spareFile;
	std::vector<unsigned char> spareFrame;
	int spareW = 0;
	int spareH = 0;
	std::mutex jobMutex;
	std::condition_variable jobReady;
	LoadRequest job{};
	bool retire = false;
	std::atomic<bool> quit{false};
	std::thread worker;

	mutable std::mutex loadMutex;
	uint64_t loadId = 0;
	LoadPhase loadPhase = LoadPhase::Idle;
	bool dropDub = false;
	LoadRequest loadPending{};
	std::string loadReason;
	bool committedWebcam = false;
	std::string committedPath;
	int committedWebcamIndex = 0;

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
