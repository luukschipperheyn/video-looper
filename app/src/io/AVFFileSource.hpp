#pragma once

#include "io/VideoSource.hpp"
#include <string>
#include <utility>
#include <vector>

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
	// Decodes the next frame into bgra (BGRA, bottom row first) without touching GL.
	bool copyFrame(std::vector<unsigned char>& bgra, int& outW, int& outH);
	void swap(AVFFileSource& other) { std::swap(impl, other.impl); }
	float nominalFps() const;

	// Normalized loop range [0..1], start < end. Applies immediately.
	void setLoopRange(float startPct, float endPct);

	// Restart playback at current loop start immediately
	void restartAtLoopStart();

	// True after a non-looping read passes the end of the range, until the reader is recreated.
	bool ended() const;

private:
	struct Impl;
	Impl* impl;

	// Recreate the AVAssetReader at the current loop range start
	bool recreateReaderAtRangeStart();
	// Advances framesToAdvance samples and copies the last into the frame buffer. False at the end of the range.
	bool decode(int framesToAdvance);
};
