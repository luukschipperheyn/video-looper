#include "io/Export.hpp"

#include "control/Instrument.hpp"
#include "gl/PictureRead.hpp"
#include "io/Recorder.hpp"
#include "ui/ParamBus.hpp"

Export::Export(ParamBus& params, Instrument& instrument, Recorder& recorder, PictureRead& pictures)
	: params(params), instrument(instrument), recorder(recorder), pictures(pictures) {}

void Export::resized() {
	if (phase == Phase::On) instrument.setSwitch("record_enabled", false);
}

void Export::frame(double dt, Picture picture, int width, int height) {
	if (params.getBool("record_enabled") && phase == Phase::Off) begin(width, height);
	if (!params.getBool("record_enabled") && phase == Phase::On) end();
	if (phase != Phase::On) return;
	accum += dt;
	while (accum + 1e-6 >= period) {
		accum -= period;
		const std::vector<unsigned char>& pixels = pictures.read(picture);
		if (!recorder.appendBGRAFrame(pixels.data(), width * 4)) return;
	}
}

const std::string& Export::failure() const {
	return reason;
}

void Export::begin(int width, int height) {
	double fps = params.getFloat("record_fps");
	if (!(fps > 0.0)) fps = 60.0;
	period = 1.0 / fps;
	accum = 0.0;
	if (!recorder.start(params.getString("record_path"), width, height, fps)) {
		reason = "could not start";
		instrument.setSwitch("record_enabled", false);
		return;
	}
	reason.clear();
	phase = Phase::On;
}

void Export::end() {
	recorder.stop();
	phase = Phase::Off;
}
