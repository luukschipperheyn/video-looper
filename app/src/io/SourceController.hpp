#pragma once

#include <OpenGL/gl3.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <string>

#include "ui/ParamBus.hpp"
#include "io/VideoSource.hpp"
#include "io/AVFWebcamSource.hpp"
#include "io/AVFFileSource.hpp"
#include "io/PageCache.hpp"
#include "io/SourceLoad.hpp"

class SourceController {
public:
	SourceController();
	~SourceController();
	SourceController(const SourceController&) = delete;
	SourceController& operator=(const SourceController&) = delete;

	void init(int width, int height);
	void resize(int width, int height);
	void shutdown();

	// Advance sources and populate preview texture. Returns true if a new frame was uploaded.
	bool tickAndGrab(double nowSeconds, double dtSeconds, ParamBus& params);

	// Load a random Video from the current Videos folder as the Source. Does not Commit.
	void triggerLoadRandomFile(ParamBus& params);

	// Restart file playback at current loop start if file source is active
	void restartFileAtLoopStart();

	// Load previous/next video from the current videos folder. Loads run off the render thread; the live Source
	// keeps playing until the new one has a first frame.
	void loadPrevFile(ParamBus& params);
	void loadNextFile(ParamBus& params);
	void loadVideo(const std::string& path, float loopStart = 0, float loopLength = 1);
	// Selects path and Commits it into the Loop once it is the Source.
	void queueDub(const std::string& path, float loopStart, float loopLength);
	// Opens these Videos ahead of a tap. paths are full paths. An empty list drops the page.
	void primePage(const std::vector<std::string>& paths, float loopStart, float loopLength);
	void loadWebcam(int index);
	// Why the latest load failed, once. Empty if none.
	std::string takeLoadFailure();
	// True on the frame a load replaced the live Source.
	bool loadSwapped() const { return swappedLoad; }
	// Full paths of the Videos in the current Videos folder, as of the last scan.
	std::vector<std::string> videos() const { return videoFiles; }
	// Makes path the current Videos folder with videos as its Videos, without reading the disk. The live Source
	// keeps playing.
	void showFolder(const std::string& path, const std::vector<std::string>& videos, ParamBus& params);

	// Query information for UI/debugging
	GLuint getPreviewTexture() const { return previewTex; }
	GLuint getPreviewFbo() const { return previewFbo; }
	// Latest file or webcam frame at its own pixel size, before it is fitted into the loop frame.
	GLuint frameTexture() const { return frameTex; }
	int frameWidth() const { return frameW; }
	int frameHeight() const { return frameH; }
	float getFileNominalFps() const { return fileSrc.nominalFps(); }

private:
	void allocatePreview(int width, int height);
	void destroyPreview();
	void ensureFrameTexture();
	bool swapInLoaded(ParamBus& params);
	void ensureSource(ParamBus& params);
	// Applies a queued Select or Dub. True when a ready Video was swapped in and its first frame presented.
	bool applyRequest(double nowSeconds, ParamBus& params);
	bool installReady(const std::string& path, float loopStart, float loopLength, ParamBus& params);
	void beginCommit(double nowSeconds, ParamBus& params, const std::string& path);
	void cancelCommit(ParamBus& params);
	void presentFrame(const std::vector<unsigned char>& bgra, int width, int height);
	void queueVideo(const std::string& path, float loopStart, float loopLength, bool dub);
	void startLoad(const LoadRequest& request);
	void runLoads();
	std::string openSpare(const LoadRequest& request);
	void maybeRescanFolder(ParamBus& params);
	void maybeStartRandomCommit(double nowSeconds, ParamBus& params);
	void updateCommitProgress(bool updatedPreviewTex, ParamBus& params);

	int w;
	int h;
	GLuint previewTex;
	GLuint previewFbo;
	GLuint frameTex;
	int frameW;
	int frameH;

	AVFWebcamSource webcam;
	bool webcamActive;
	AVFFileSource fileSrc;
	bool fileActive;
	int seenKind;
	int seenWebcamIndex;
	bool swappedLoad = false;

	// The worker owns the spares and spareFrame from a request until it resolves; after a swap they hold the
	// previous live sources until the worker stops them.
	SourceLoad load;
	AVFWebcamSource spareWebcam;
	AVFFileSource spareFile;
	std::vector<unsigned char> spareFrame;
	int spareW;
	int spareH;
	std::mutex jobMutex;
	std::condition_variable jobReady;
	LoadRequest job;
	bool retire;
	std::atomic<bool> quit;
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
	bool commitRequested;
	bool committing;
	double commitStartTime;
	float commitNominalFps;
	int commitFramesWritten;
	bool savedLoopSetting;
};
