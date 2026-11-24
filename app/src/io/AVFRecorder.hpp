#pragma once

#include <string>

#include "io/Recorder.hpp"

class AVFRecorder : public Recorder {
public:
	AVFRecorder();
	~AVFRecorder() override;

	bool start(const std::string& outputPath, int width, int height, double fps) override;
	void stop() override;
	bool isRecording() const override;

	// Append one BGRA frame. strideBytes is bytes per row.
	bool appendBGRAFrame(const unsigned char* data, int strideBytes) override;

private:
	struct Impl;
	Impl* impl;
};
