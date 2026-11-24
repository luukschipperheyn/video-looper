#pragma once

#include <string>

#include "gl/PictureChoice.hpp"

class Instrument;
class ParamBus;
class PictureRead;
class Recorder;

class Export {
public:
	Export(ParamBus& params, Instrument& instrument, Recorder& recorder, PictureRead& pictures);

	void resized();
	void frame(double dt, Picture picture, int width, int height);
	const std::string& failure() const;

private:
	enum class Phase { Off, On };

	void begin(int width, int height);
	void end();

	ParamBus& params;
	Instrument& instrument;
	Recorder& recorder;
	PictureRead& pictures;
	Phase phase = Phase::Off;
	double accum = 0;
	double period = 1.0 / 60.0;
	std::string reason;
};
