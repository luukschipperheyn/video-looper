#include "control/Instrument.hpp"
#include "gl/PictureChoice.hpp"
#include "gl/PictureRead.hpp"
#include "io/Export.hpp"
#include "io/FolderList.hpp"
#include "io/Recorder.hpp"
#include "remote/RemoteApply.hpp"
#include "ui/ParamBus.hpp"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) ++failures;
}

struct FakeSource : SourcePort {
	void loadVideo(const std::string&, float, float) override {}
	void queueDub(const std::string&, float, float) override {}
	void primePage(const std::vector<std::string>&, float, float) override {}
	void loadWebcam(int) override {}
	void choose(SourceKind) override {}
	void setPlayback(bool, bool, float, float) override {}
	void restartFileAtLoopStart() override {}
	SourceStatus status() const override { return {}; }
};

struct FakeRecorder : Recorder {
	bool failStart = false;
	bool ready = true;
	bool on = false;
	int appends = 0;
	int written = 0;
	bool start(const std::string&, int, int, double) override {
		if (failStart) return false;
		on = true;
		return true;
	}
	void stop() override { on = false; }
	bool isRecording() const override { return on; }
	bool appendBGRAFrame(const unsigned char*, int) override {
		++appends;
		if (!ready || !on) return false;
		++written;
		return true;
	}
};

struct FakePictures : PictureRead {
	Picture asked = Picture::Audition;
	std::vector<unsigned char> pixels{0, 0, 0, 0};
	const std::vector<unsigned char>& read(Picture picture) override {
		asked = picture;
		return pixels;
	}
};

int main() {
	ParamBus params;
	FakeSource source;
	FolderList folders;
	Instrument instrument(params, source, folders, [](float) {});
	FakeRecorder recorder;
	FakePictures pictures;
	Export exp(params, instrument, recorder, pictures);

	check(instrument.setText("record_path", "/tmp/out.mov").ok, "1. the export path is set");
	recorder.failStart = true;
	check(instrument.setSwitch("record_enabled", true).ok && params.getBool("record_enabled"),
	      "1. Export turns on through the Instrument");
	Picture name = choosePictures(PictureMoment::Idle, 0).exported;
	exp.frame(1.0 / 60.0, name, 16, 16);
	std::string snap = remoteSnapshot(params, {}, {}, {}, false, "", exp.failure());
	check(!params.getBool("record_enabled") && snap.find(R"("export_error":"could not start")") != std::string::npos,
	      "1. a failed start turns Export off with a reason the page can show");

	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
