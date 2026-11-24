#include "control/Hotkeys.hpp"
#include "control/Instrument.hpp"
#include "io/FolderList.hpp"
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
	std::vector<std::string> list;
	std::vector<std::string> loads;
	std::vector<int> webcams;
	void loadVideo(const std::string& path, float, float) override { loads.push_back(path); }
	void queueDub(const std::string&, float, float) override {}
	void primePage(const std::vector<std::string>&, float, float) override {}
	void loadWebcam(int index) override { webcams.push_back(index); }
	void showFolder(const std::string&, const std::vector<std::string>& videos) override { list = videos; }
	void choose(SourceKind) override {}
	void setPlayback(bool, bool, float, float) override {}
	void restartFileAtLoopStart() override {}
	SourceStatus status() const override { return {}; }
	std::vector<std::string> videos() const override { return list; }
};

int main() {
	ParamBus params;
	FakeSource source;
	FolderList folders;
	Instrument instrument(params, source, folders, {});
	source.list = {"/v/a.mov", "/v/b.mov"};

	{
		params.setInt("source_kind", 0);
		Hotkeys hotkeys;
		KeysDown down;
		down.sourceNext = true;
		hotkeys.frame(instrument, down, {}, false);
		check(params.getInt("source_kind") == 1, "1. Equal from Shader reaches File");
		hotkeys.frame(instrument, down, {}, false);
		check(params.getInt("source_kind") == 1, "1. Equal held does not move");
		down.sourceNext = false;
		hotkeys.frame(instrument, down, {}, false);
		down.sourceNext = true;
		hotkeys.frame(instrument, down, {}, false);
		check(params.getInt("source_kind") == 2, "1. Equal pressed again reaches Black");
		down.sourceNext = false;
		hotkeys.frame(instrument, down, {}, false);
		down.sourceNext = true;
		int cams = int(source.webcams.size());
		hotkeys.frame(instrument, down, {}, false);
		check(params.getInt("source_kind") == 2 && int(source.webcams.size()) == cams + 1,
		      "1. Equal again asks for a webcam and leaves the kind at Black");
		params.setInt("source_kind", 3);
		down.sourceNext = false;
		hotkeys.frame(instrument, down, {}, false);
		down.sourceNext = true;
		cams = int(source.webcams.size());
		hotkeys.frame(instrument, down, {}, false);
		check(params.getInt("source_kind") == 3 && int(source.webcams.size()) == cams,
		      "1. Equal from Webcam does not make it Draw");
	}

	{
		params.setInt("source_kind", 4);
		int cams = int(source.webcams.size());
		Hotkeys hotkeys;
		KeysDown down;
		down.sourcePrev = true;
		hotkeys.frame(instrument, down, {}, false);
		check(params.getInt("source_kind") == 2 && int(source.webcams.size()) == cams,
		      "2. Minus while Draw lands on Black");
	}

	{
		params.setInt("source_kind", 0);
		int loads = int(source.loads.size());
		int cams = int(source.webcams.size());
		bool show = params.getBool("show_ui");
		Hotkeys hotkeys;
		KeysDown down;
		HotkeyHolds levels;
		down.showUi = true;
		down.sourceNext = true;
		down.randomVideo = true;
		levels.overdub = true;
		levels.cue = true;
		HotkeyHolds held = hotkeys.frame(instrument, down, levels, true);
		check(!held.overdub && !held.cue && params.getBool("show_ui") == show && params.getInt("source_kind") == 0 &&
		          int(source.loads.size()) == loads && int(source.webcams.size()) == cams,
		      "3. a text field focused changes nothing and both holds stay closed");
		held = hotkeys.frame(instrument, down, levels, false);
		check(held.overdub && held.cue && params.getBool("show_ui") == show && params.getInt("source_kind") == 0 &&
		          int(source.loads.size()) == loads && int(source.webcams.size()) == cams,
		      "3. focus gone with those keys still down changes nothing, and the holds are open");
	}

	{
		Hotkeys hotkeys;
		bool show = params.getBool("show_ui");
		KeysDown down;
		down.showUi = true;
		hotkeys.frame(instrument, down, {}, false);
		check(params.getBool("show_ui") == !show, "4. U toggles show_ui once");
		hotkeys.frame(instrument, down, {}, false);
		check(params.getBool("show_ui") == !show, "4. U held does not toggle again");

		down = {};
		hotkeys.frame(instrument, down, {}, false);
		down.digit[5] = true;
		hotkeys.frame(instrument, down, {}, false);
		check(params.getFloat("record_blend_opacity") == 0.5f, "4. the 5 key sets opacity to 0.5");
		params.setFloat("record_blend_opacity", 0.25f);
		hotkeys.frame(instrument, down, {}, false);
		check(params.getFloat("record_blend_opacity") == 0.25f, "4. a held 5 does not set opacity again");
		down = {};
		hotkeys.frame(instrument, down, {}, false);
		down.digit[0] = true;
		hotkeys.frame(instrument, down, {}, false);
		check(params.getFloat("record_blend_opacity") == 1.0f, "4. the 0 key sets opacity to 1.0");

		source.loads.clear();
		down = {};
		hotkeys.frame(instrument, down, {}, false);
		down.randomVideo = true;
		hotkeys.frame(instrument, down, {}, false);
		check(source.loads.size() == 1, "4. V loads one Video");
		hotkeys.frame(instrument, down, {}, false);
		check(source.loads.size() == 1, "4. a held V does not load another");

		params.setInt("record_blend_mode", 9);
		down = {};
		hotkeys.frame(instrument, down, {}, false);
		down.blendNext = true;
		hotkeys.frame(instrument, down, {}, false);
		check(params.getInt("record_blend_mode") == 10, "4. ] steps record_blend_mode by one");
		hotkeys.frame(instrument, down, {}, false);
		check(params.getInt("record_blend_mode") == 10, "4. a held ] does not step again");
		down = {};
		hotkeys.frame(instrument, down, {}, false);
		params.setInt("record_blend_mode", 11);
		down.blendNext = true;
		hotkeys.frame(instrument, down, {}, false);
		check(params.getInt("record_blend_mode") == 11, "4. ] stops at 11");

		params.setInt("fx_mode", 1);
		down = {};
		hotkeys.frame(instrument, down, {}, false);
		down.fxPrev = true;
		hotkeys.frame(instrument, down, {}, false);
		check(params.getInt("fx_mode") == 0, "4. apostrophe steps fx_mode down to 0");
		down = {};
		hotkeys.frame(instrument, down, {}, false);
		down.fxPrev = true;
		hotkeys.frame(instrument, down, {}, false);
		check(params.getInt("fx_mode") == 0, "4. apostrophe at fx_mode 0 stays 0");
	}

	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
