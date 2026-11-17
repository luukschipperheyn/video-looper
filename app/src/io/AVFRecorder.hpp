#pragma once

#include <string>

class AVFRecorder {
public:
	AVFRecorder();
	~AVFRecorder();

	bool start(const std::string& outputPath, int width, int height, double fps);
	void stop();
	bool isRecording() const;

	// Append one BGRA frame. strideBytes is bytes per row.
	bool appendBGRAFrame(const unsigned char* data, int strideBytes);

private:
	struct Impl;
	Impl* impl;
};
