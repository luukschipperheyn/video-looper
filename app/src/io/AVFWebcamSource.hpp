#pragma once

#include "io/VideoSource.hpp"

class AVFWebcamSource : public IVideoSource {
public:
	AVFWebcamSource();
	~AVFWebcamSource() override;
	bool start(int deviceIndex) override;
	void stop() override;
	bool isOpen() const override;
	bool grab(GLuint destTex, int& outW, int& outH) override;

	struct Impl; // make visible for ObjC property type

private:
	Impl* impl;
};
