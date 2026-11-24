#pragma once

#include <string>

class Recorder {
public:
	virtual ~Recorder() = default;
	virtual bool start(const std::string& outputPath, int width, int height, double fps) = 0;
	virtual void stop() = 0;
	virtual bool isRecording() const = 0;
	// False when this frame cannot be taken. The caller drops it and returns.
	virtual bool appendBGRAFrame(const unsigned char* data, int strideBytes) = 0;
};
