#include "io/Export.hpp"

#include "control/Instrument.hpp"
#include "gl/PictureRead.hpp"
#include "io/Recorder.hpp"
#include "ui/ParamBus.hpp"

Export::Export(ParamBus& params, Instrument& instrument, Recorder& recorder, PictureRead& pictures)
	: params(params), instrument(instrument), recorder(recorder), pictures(pictures) {}

void Export::resized() {}

void Export::frame(double, Picture, int, int) {
	if (!params.getBool("record_enabled") || recorder.isRecording()) return;
	if (!recorder.start(params.getString("record_path"), 0, 0, params.getFloat("record_fps")))
		params.setBool("record_enabled", false);
}

const std::string& Export::failure() const {
	return reason;
}
