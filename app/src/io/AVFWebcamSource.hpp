#pragma once

#include <OpenGL/gl3.h>

#include "io/VideoSource.hpp"

#include <utility>
#include <vector>

class AVFWebcamSource : public CameraDecoder {
public:
	AVFWebcamSource();
	~AVFWebcamSource() override;
	bool start(int deviceIndex) override;
	void stop() override;
	bool isOpen() const;
	bool grab(GLuint destTex, int& outW, int& outH) override;
	// Copies the newest unseen frame into bgra (BGRA, bottom row first) without touching GL.
	bool copyFrame(std::vector<unsigned char>& bgra, int& outW, int& outH) override;

	struct Impl; // make visible for ObjC property type

private:
	Impl* impl;
};
