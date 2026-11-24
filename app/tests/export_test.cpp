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
	int stops = 0;
	bool start(const std::string&, int, int, double) override {
		if (failStart) return false;
		on = true;
		return true;
	}
	void stop() override {
		on = false;
		++stops;
	}
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

struct Rig {
	ParamBus params;
	FakeSource source;
	FolderList folders;
	FakeRecorder recorder;
	FakePictures pictures;
	Instrument instrument;
	Export exp;
	Rig() : instrument(params, source, folders, [](float) {}), exp(params, instrument, recorder, pictures) {}
};

static void arm(Rig& rig, double fps) {
	rig.instrument.setText("record_path", "/tmp/out.mov");
	rig.instrument.setNumber("record_fps", fps);
	rig.instrument.setSwitch("record_enabled", true);
}

int main() {
	{
		Rig rig;
		check(rig.instrument.setText("record_path", "/tmp/out.mov").ok, "1. the export path is set");
		rig.recorder.failStart = true;
		check(rig.instrument.setSwitch("record_enabled", true).ok && rig.params.getBool("record_enabled"),
		      "1. Export turns on through the Instrument");
		Picture name = choosePictures(PictureMoment::Idle, 0, 1).exported;
		rig.exp.frame(1.0 / 60.0, name, 16, 16);
		std::string snap = remoteSnapshot(rig.params, {}, {}, {}, false, "", rig.exp.failure());
		check(!rig.params.getBool("record_enabled") &&
		          snap.find(R"("export_error":"could not start")") != std::string::npos,
		      "1. a failed start turns Export off with a reason the page can show");
	}

	{
		Rig rig;
		arm(rig, 24);
		Picture name = choosePictures(PictureMoment::Overdub, 0.25f, 1).exported;
		for (int i = 0; i < 60; ++i)
			rig.exp.frame(1.0 / 60.0, name, 8, 8);
		check(rig.recorder.written == 24, "2. 24 fps against a 60 Hz frame loop writes 24 frames in one second");
		check(rig.pictures.asked == name && name == Picture::Program,
		      "2. Export asks the picture path for the Program by the name choosePictures returns");
		int written = rig.recorder.written;
		rig.instrument.setSwitch("record_enabled", false);
		rig.exp.frame(1.0 / 60.0, name, 8, 8);
		check(!rig.recorder.on && rig.recorder.written == written && rig.exp.failure().empty(),
		      "2. turning Export off through the Instrument stops the writer");
	}

	{
		Rig rig;
		arm(rig, 24);
		Picture name = choosePictures(PictureMoment::Idle, 0, 1).exported;
		rig.recorder.ready = false;
		rig.exp.frame(1.0 / 24.0, name, 8, 8);
		check(rig.recorder.written == 0 && rig.recorder.appends == 1 && rig.params.getBool("record_enabled"),
		      "3. a writer that is not ready drops that frame and returns");
		rig.recorder.ready = true;
		rig.exp.frame(1.0 / 24.0, name, 8, 8);
		check(rig.recorder.written == 1, "3. the next due frame is written when the writer is ready");
	}

	{
		Rig rig;
		arm(rig, 24);
		Picture name = choosePictures(PictureMoment::Idle, 0, 1).exported;
		rig.exp.frame(0, name, 16, 16);
		check(rig.recorder.on, "4. Export is on");
		rig.exp.resized();
		rig.exp.frame(1.0 / 24.0, name, 32, 32);
		std::string snap = remoteSnapshot(rig.params, {}, {}, {}, false, "", rig.exp.failure());
		check(!rig.params.getBool("record_enabled") && !rig.recorder.on && rig.recorder.written == 0 &&
		          snap.find(R"("export_error":"")") != std::string::npos,
		      "4. a size change turns Export off through the Instrument");
	}

	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
