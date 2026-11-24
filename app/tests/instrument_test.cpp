#include "control/ControlTable.hpp"
#include "control/Instrument.hpp"
#include "io/FolderList.hpp"
#include "setup/SetupStore.hpp"
#include "ui/ParamBus.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) ++failures;
}

struct Loaded {
	std::string path;
	float start;
	float length;
};

struct FakeSource : SourcePort {
	std::vector<std::string> list;
	std::vector<Loaded> loads;
	std::vector<Loaded> dubs;
	std::vector<std::string> page;
	std::vector<int> webcams;
	void loadVideo(const std::string& path, float start, float length) override {
		loads.push_back({path, start, length});
	}
	void queueDub(const std::string& path, float start, float length) override {
		dubs.push_back({path, start, length});
	}
	void primePage(const std::vector<std::string>& paths, float, float) override { page = paths; }
	void loadWebcam(int index) override { webcams.push_back(index); }
	void showFolder(const std::string&, const std::vector<std::string>& videos) override { list = videos; }
	void choose(SourceKind) override {}
	void setPlayback(bool, bool, float, float) override {}
	void restartFileAtLoopStart() override {}
	SourceStatus status() const override { return {}; }
	std::vector<std::string> videos() const override { return list; }
};

int main() {
	{
		std::set<std::string> names;
		bool unique = true, ranged = true, defaultsInRange = true;
		for (const Control& c : kControls) {
			unique = unique && names.insert(c.name).second;
			bool number = c.type == ControlType::Int || c.type == ControlType::Float;
			if (number && c.web == Web::Write) ranged = ranged && c.min < c.max;
			if (number && c.min < c.max) defaultsInRange = defaultsInRange && c.def >= c.min && c.def <= c.max;
		}
		check(unique, "1. every control is listed once");
		check(ranged, "1. every number the Web interface may set has a range");
		check(defaultsInRange, "1. every default sits inside its range");

		ParamBus fresh;
		check(fresh.getFloat("bpm") == 120.0f && fresh.getBool("tempo_coupled") &&
		          fresh.getInt("source_fx_mode") == 5 && fresh.getBool("env_enabled") &&
		          fresh.getString("file_path").empty(),
		      "1. a fresh ParamBus holds the table's defaults");
		PerformanceSetup blank;
		check(blank.values.getFloat("bpm") == fresh.getFloat("bpm") &&
		          blank.values.getInt("source_fx_mode") == fresh.getInt("source_fx_mode"),
		      "1. a setup with nothing in it restores those same defaults");
		for (const char* name : {"arm_record", "cue_active", "record_enabled"}) {
			const Control* c = findControl(name);
			check(c && c->keep == Keep::StartsOff && c->def == 0.0f, "1. Latch, Cue, and Export start off");
		}
	}

	ParamBus params;
	FakeSource source;
	FolderList folders;
	float linkTempo = -1.0f;
	Instrument instrument(params, source, folders, [&](float bpm) { linkTempo = bpm; });

	check(instrument.setNumber("bpm", 999).ok && params.getFloat("bpm") == 240.0f,
	      "2. a number is clamped to its range");
	check(instrument.setNumber("webcam_index", -3).ok && params.getInt("webcam_index") == 0,
	      "2. the webcam index is clamped from either control surface");
	check(instrument.setNumber("record_blend_mode", 4.6).ok && params.getInt("record_blend_mode") == 5,
	      "2. an Int control is rounded");
	check(instrument.setNumber("fx_mode", 99).ok && params.getInt("fx_mode") == 6,
	      "2. stepping past the last mode stops there");
	Outcome bad = instrument.setNumber("env_enabled", 1);
	check(!bad.ok && bad.topic == "env_enabled" && bad.reason == "bad value" && params.getBool("env_enabled"),
	      "2. a number for a switch is refused");
	check(!instrument.setSwitch("bpm", true).ok && !instrument.setNumber("nope", 1).ok &&
	          !instrument.setText("bpm", "x").ok && !instrument.setNumber("bpm", std::nan("")).ok,
	      "2. the wrong type, an unknown control, and a non-number are refused");
	check(linkTempo == -1.0f, "3. with Link off, the tempo is not handed to Link");
	params.setBool("link_enabled", true);
	instrument.setNumber("bpm", 133);
	check(linkTempo == 133.0f, "3. with Link on, a new tempo is handed to Link");
	linkTempo = -1.0f;
	instrument.setNumber("bpm", 133);
	check(linkTempo == -1.0f, "3. the same tempo again is not");

	Outcome noPath = instrument.setSwitch("record_enabled", true);
	check(!noPath.ok && noPath.reason == "no export path" && !params.getBool("record_enabled"),
	      "4. Export with no export path is refused");
	check(instrument.setText("record_path", "/tmp/out.mov").ok && instrument.setSwitch("record_enabled", true).ok &&
	          params.getBool("record_enabled"),
	      "4. with a path, Export turns on");

	params.setFloat("video_sx", 2.0f);
	params.setFloat("video_sy", 0.5f);
	instrument.setSwitch("video_aspect", false);
	check(params.getFloat("video_sy") == 0.5f, "5. turning Preserve aspect off leaves scale y");
	instrument.setSwitch("video_aspect", true);
	check(params.getFloat("video_sy") == 2.0f, "5. turning it on copies scale x onto scale y");

	params.setInt("webcam_index", 2);
	instrument.setNumber("source_kind", 1);
	check(instrument.setNumber("source_kind", 3).ok && params.getInt("source_kind") == 1 &&
	          source.webcams == std::vector<int>{2},
	      "6. choosing the webcam starts a webcam load and leaves the Source kind until it swaps in");
	instrument.chooseWebcam();
	check(source.webcams.size() == 2, "6. the webcam can be asked for again");

	params.setBool("arm_record", true);
	params.setBool("cue_active", true);
	instrument.panic();
	check(!params.getBool("arm_record") && !params.getBool("cue_active"),
	      "7. Panic releases the Latch and the latched Cue");

	source.list = {"/v/a.mov", "/v/b.mov", "/v/c.mov"};
	instrument.setNumber("file_loop_start", 0.25);
	instrument.setNumber("file_loop_length", 0.5);
	params.setString("file_path", "/v/a.mov");
	instrument.nextVideo();
	check(source.loads.size() == 1 && source.loads[0].path == "/v/b.mov" && source.loads[0].start == 0.25f &&
	          source.loads[0].length == 0.5f,
	      "8. Next loads the following Video at the loop range");
	instrument.prevVideo();
	check(source.loads.back().path == "/v/c.mov", "8. Prev wraps to the last Video");
	instrument.randomVideo();
	check(source.loads.size() == 3 && source.loads.back().path.rfind("/v/", 0) == 0,
	      "8. random loads a Video from the folder");
	check(instrument.loadVideoNamed("c.mov").ok && source.loads.back().path == "/v/c.mov",
	      "8. a Video is loaded by filename");
	Outcome missing = instrument.loadVideoNamed("zzz.mov");
	check(!missing.ok && missing.topic == "source" && missing.reason == "no such video" && source.loads.size() == 4,
	      "8. an unknown filename is refused and nothing loads");
	check(instrument.dubVideoNamed("b.mov").ok && source.dubs.size() == 1 && source.dubs[0].path == "/v/b.mov" &&
	          source.dubs[0].start == 0.25f && source.loads.size() == 4,
	      "9. Dub asks the Source to Commit that Video");
	check(!instrument.dubVideoNamed("zzz.mov").ok && source.dubs.size() == 1,
	      "9. a Dub of an unknown filename is refused");
	instrument.primePage({"c.mov", "zzz.mov", "a.mov"});
	check(source.page == std::vector<std::string>{"/v/c.mov", "/v/a.mov"},
	      "9. a page opens the Videos it can find, in order");

	FakeSource empty;
	Instrument bare(params, empty, folders, {});
	bare.nextVideo();
	bare.randomVideo();
	check(empty.loads.empty(), "10. with no Videos, Next and random do nothing");

	instrument.addFolder("/w", {"/w/x.mov"});
	check(source.list == std::vector<std::string>{"/w/x.mov"} && params.getString("videos_folder") == "/w" &&
	          params.getInt("videos_count") == 1 && folders.current() && folders.current()->path == "/w",
	      "11. adding a folder makes it the Videos folder");
	instrument.addFolder("/v", {"/v/a.mov", "/v/b.mov"});
	check(instrument.chooseFolder("/w").ok && params.getString("videos_folder") == "/w" &&
	          params.getInt("videos_count") == 1,
	      "11. a listed folder can be chosen again");
	Outcome nowhere = instrument.chooseFolder("/nowhere");
	check(!nowhere.ok && nowhere.topic == "folder" && nowhere.reason == "no such folder" &&
	          params.getString("videos_folder") == "/w",
	      "11. an unlisted folder is refused and the Videos folder stays");

	params.setInt("source_kind", 0);
	int cams = int(source.webcams.size());
	instrument.stepSourceKind(+1);
	check(params.getInt("source_kind") == 1, "12. plus from Shader lands on File");
	instrument.stepSourceKind(+1);
	check(params.getInt("source_kind") == 2, "12. plus from File lands on Black");
	instrument.stepSourceKind(+1);
	check(params.getInt("source_kind") == 2 && int(source.webcams.size()) == cams + 1,
	      "12. plus from Black asks for a webcam and leaves the kind at Black");
	params.setInt("source_kind", 3);
	cams = int(source.webcams.size());
	instrument.stepSourceKind(+1);
	check(params.getInt("source_kind") == 3 && int(source.webcams.size()) == cams,
	      "12. plus from Webcam leaves Webcam and does not ask for another");
	params.setInt("source_kind", 4);
	instrument.stepSourceKind(+1);
	check(params.getInt("source_kind") == 4 && int(source.webcams.size()) == cams,
	      "12. plus from Draw leaves Draw");

	instrument.stepSourceKind(-1);
	check(params.getInt("source_kind") == 2 && int(source.webcams.size()) == cams,
	      "13. minus from Draw lands on Black and does not ask for a webcam");
	params.setInt("source_kind", 0);
	instrument.stepSourceKind(-1);
	check(params.getInt("source_kind") == 0, "13. minus from Shader stays Shader");
	params.setInt("source_kind", 3);
	instrument.stepSourceKind(-1);
	check(params.getInt("source_kind") == 2 && int(source.webcams.size()) == cams,
	      "13. minus from Webcam lands on Black");

	params.setInt("record_blend_mode", 11);
	check(instrument.nudge("record_blend_mode", 1).ok && params.getInt("record_blend_mode") == 11,
	      "14. nudge stops at the end of the range");
	params.setInt("source_kind", 0);
	Outcome kindNudge = instrument.nudge("source_kind", 1);
	check(!kindNudge.ok && kindNudge.reason == "bad value" && params.getInt("source_kind") == 0 &&
	          int(source.webcams.size()) == cams,
	      "14. nudge of source_kind is refused and the kind stays");
	bool shown = params.getBool("show_ui");
	check(instrument.toggle("show_ui").ok && params.getBool("show_ui") == !shown, "14. toggle flips show_ui");
	float bpm = params.getFloat("bpm");
	Outcome toggled = instrument.toggle("bpm");
	check(!toggled.ok && toggled.reason == "bad value" && params.getFloat("bpm") == bpm,
	      "14. toggle of a number is refused");

	{
		ParamBus fresh;
		FakeSource sink;
		FolderList listed;
		Instrument mac(fresh, sink, listed, {});
		auto dir = std::filesystem::temp_directory_path() / "video-looper-mac-folder";
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
		std::ofstream{(dir / "a.mov").string()};
		mac.setText("videos_folder", dir.string());
		const PreparedFolder* current = listed.current();
		const std::vector<std::string> videos{(dir / "a.mov").string()};
		check(current && current->path == dir.string() && listed.paths() == std::vector<std::string>{dir.string()} &&
		          fresh.getString("videos_folder") == dir.string() && current->videos == videos,
		      "15. setting the Videos folder the way the Mac button does lists it and makes it current");
		std::filesystem::remove_all(dir);
	}

	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
