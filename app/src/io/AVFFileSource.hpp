#pragma once

#include "io/VideoSource.hpp"
#include <string>

class AVFFileSource : public IVideoSource {
public:
	AVFFileSource();
	~AVFFileSource() override;
	bool startFile(const std::string& path) override;
	void setPaused(bool p) override;
	void setLoop(bool l) override;
	void tick(double dt) override;
	void stop() override;
	bool isOpen() const override;
	bool grab(GLuint destTex, int& outW, int& outH) override;
	float nominalFps() const;

	// Normalized loop range [0..1], start < end. Applies immediately.
	void setLoopRange(float startPct, float endPct);

	// Restart playback at current loop start immediately
	void restartAtLoopStart();

private:
	struct Impl;
	Impl* impl;

	// Recreate the AVAssetReader at the current loop range start
	bool recreateReaderAtRangeStart();
};
