#pragma once

#include <string>

#include "gl/PictureChoice.hpp"

class Instrument;
class ParamBus;
class PictureRead;
class Recorder;

// The life of Export: on, off, a failed start, and which frames are written.
class Export {
public:
	Export(ParamBus& params, Instrument& instrument, Recorder& recorder, PictureRead& pictures);

	void resized();
	void frame(double dt, Picture name, int width, int height);
	const std::string& failure() const;

private:
	ParamBus& params;
	Instrument& instrument;
	Recorder& recorder;
	PictureRead& pictures;
	bool on = false;
	std::string reason;
};
