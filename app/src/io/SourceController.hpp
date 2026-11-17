#pragma once

#include <OpenGL/gl3.h>
#include <vector>
#include <string>

#include "ui/ParamBus.hpp"
#include "io/VideoSource.hpp"
#include "io/AVFWebcamSource.hpp"
#include "io/AVFFileSource.hpp"

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

    // Load a random file from the current folder selection immediately
    void triggerLoadRandomFile();

	// Restart file playback at current loop start if file source is active
	void restartFileAtLoopStart();

	// Load previous/next video from the current videos folder
	void loadPrevFile(ParamBus& params);
	void loadNextFile(ParamBus& params);

	// Query information for UI/debugging
	GLuint getPreviewTexture() const { return previewTex; }
	GLuint getPreviewFbo() const { return previewFbo; }
	float getFileNominalFps() const { return fileSrc.nominalFps(); }

private:
	void allocatePreview(int width, int height);
	void destroyPreview();
	void ensureSource(ParamBus& params);
	void maybeRescanFolder(ParamBus& params);
	void maybeStartRandomCommit(double nowSeconds, ParamBus& params);
	void updateCommitProgress(bool updatedPreviewTex, ParamBus& params);

	int w;
	int h;
	GLuint previewTex;
	GLuint previewFbo;

	AVFWebcamSource webcam;
	bool webcamActive;
	int currentWebcamIndex;
	AVFFileSource fileSrc;
	bool fileActive;

	std::vector<std::string> videoFiles;
	std::string lastVideosFolder;
	bool commitRequested;
	bool committing;
	double commitStartTime;
	float commitNominalFps;
	int commitFramesWritten;
	bool savedLoopSetting;
};


