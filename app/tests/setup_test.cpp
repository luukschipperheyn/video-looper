#include "remote/RemoteApply.hpp"
#include "setup/SetupStore.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

static int failures = 0;

static void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) ++failures;
}

static bool near(float a, float b) {
	return std::fabs(a - b) < 1e-5f;
}

int main() {
	ParamBus params;
	params.setInt("record_blend_mode", 5);
	params.setFloat("record_blend_opacity", 0.25f);
	params.setFloat("gain_mix", 0.5f);
	params.setBool("env_enabled", false);
	params.setFloat("env_attack", 0.1f);
	params.setFloat("fx_param2", 0.985f);
	params.setInt("fx_count", 6);
	params.setInt("fx_repeat_blend", 1);
	params.setBool("source_fx_enabled", true);
	params.setInt("source_fx_mode", 5);
	params.setFloat("source_fx_param1", 0.2f);
	params.setFloat("source_fx_param2", 0.25f);
	params.setInt("source_fx_count", 3);
	params.setInt("source_fx_repeat_blend", 6);
	params.setInt("source_kind", 1);
	params.setFloat("video_tx", -1.5f);
	params.setFloat("video_rot", 0.25f);
	params.setBool("video_bars", false);
	params.setBool("video_aspect", false);
	params.setInt("webcam_index", 2);
	params.setBool("file_loop", false);
	params.setBool("file_paused", true);
	params.setInt("file_run_mode", 1);
	params.setFloat("file_loop_start", 0.25f);
	params.setFloat("file_loop_length", 0.5f);
	params.setString("file_path", "/Movies/a \"b\".mov");
	params.setFloat("bpm", 173.5f);
	params.setFloat("read_speed_mult", 2);
	params.setBool("tempo_coupled", false);
	params.setBool("ipad_page", true);
	params.setString("record_path", "/tmp/out.mov");
	params.setFloat("record_fps", 24);
	params.setBool("arm_record", true);
	params.setBool("cue_active", true);
	params.setBool("record_enabled", true);
	params.setBool("link_enabled", true);
	params.setBool("show_ui", false);
	params.setBool("ipad_token", false);
	params.setFloat("quantum", 8);
	FolderList folders;
	folders.add({"/waiting", {}});
	folders.add({"/current", {}});
	folders.select("/current");
	params.setString("videos_folder", "/current");

	PerformanceSetup setup = captureSetup(params, folders);
	check(setup.folders.size() == 2 && setup.folders[0] == "/waiting" && setup.folders[1] == "/current",
	      "capture keeps the Folder list in order");
	check(setup.values.getString("videos_folder") == "/current", "capture keeps the current Videos folder");
	check(setup.values.getString("file_path") == "/Movies/a \"b\".mov", "capture keeps the current Video");

	auto dir = std::filesystem::temp_directory_path() / "video-looper-setup-test";
	std::filesystem::remove_all(dir);
	auto file = dir / "nested" / "setup.json";
	check(writeSetup(file, setup), "the setup is written");
	std::ifstream in(file);
	std::string text((std::istreambuf_iterator<char>(in)), {});
	check(text.find("arm_record") == std::string::npos && text.find("cue_active") == std::string::npos &&
	          text.find("record_enabled") == std::string::npos && text.find("link_enabled") == std::string::npos,
	      "the file leaves out the Latch, Cue, Export, and Link");

	PerformanceSetup loaded;
	check(readSetup(file, loaded), "the setup is read back");
	ParamBus fresh;
	fresh.setBool("link_enabled", true);
	fresh.setBool("show_ui", false);
	fresh.setBool("ipad_token", false);
	fresh.setFloat("quantum", 8);
	fresh.setBool("arm_record", true);
	fresh.setBool("cue_active", true);
	fresh.setBool("record_enabled", true);
	applySetup(fresh, loaded);
	check(fresh.getInt("record_blend_mode") == 5, "blend mode comes back");
	check(near(fresh.getFloat("record_blend_opacity"), 0.25f), "opacity comes back");
	check(near(fresh.getFloat("gain_mix"), 0.5f), "blend gain comes back");
	check(!fresh.getBool("env_enabled") && near(fresh.getFloat("env_attack"), 0.1f), "envelope comes back");
	check(near(fresh.getFloat("fx_param2"), 0.985f) && fresh.getInt("fx_count") == 6 &&
	          fresh.getInt("fx_repeat_blend") == 1,
	      "FX comes back");
	check(fresh.getBool("source_fx_enabled") && fresh.getInt("source_fx_mode") == 5 &&
	          near(fresh.getFloat("source_fx_param1"), 0.2f) && near(fresh.getFloat("source_fx_param2"), 0.25f) &&
	          fresh.getInt("source_fx_count") == 3 && fresh.getInt("source_fx_repeat_blend") == 6,
	      "Source FX comes back");
	check(fresh.getInt("source_kind") == 1, "a missing Video stays the Source");
	check(fresh.getString("file_path") == "/Movies/a \"b\".mov", "the Video path comes back");
	check(near(fresh.getFloat("video_tx"), -1.5f) && near(fresh.getFloat("video_rot"), 0.25f), "transform comes back");
	check(!fresh.getBool("video_bars") && !fresh.getBool("video_aspect"), "bars and aspect come back");
	check(fresh.getInt("webcam_index") == 2, "webcam comes back");
	check(!fresh.getBool("file_loop") && fresh.getBool("file_paused") && fresh.getInt("file_run_mode") == 1,
	      "video run state comes back");
	check(near(fresh.getFloat("file_loop_start"), 0.25f) && near(fresh.getFloat("file_loop_length"), 0.5f),
	      "the loop range comes back");
	check(near(fresh.getFloat("bpm"), 173.5f) && near(fresh.getFloat("read_speed_mult"), 2.f) &&
	          !fresh.getBool("tempo_coupled"),
	      "tempo comes back");
	check(fresh.getBool("ipad_page"), "the Web interface comes back on");
	check(fresh.getString("record_path") == "/tmp/out.mov" && near(fresh.getFloat("record_fps"), 24.f),
	      "the export path and fps come back");
	check(!fresh.getBool("arm_record") && !fresh.getBool("cue_active") && !fresh.getBool("record_enabled"),
	      "Latch, Cue, and Export start off");
	check(fresh.getBool("link_enabled") && !fresh.getBool("show_ui") && !fresh.getBool("ipad_token") &&
	          near(fresh.getFloat("quantum"), 8.f),
	      "Mac-only settings stay where they were");

	std::filesystem::create_directories(dir / "kept");
	std::ofstream(dir / "notafile") << "x";
	PerformanceSetup disks;
	disks.folders = {(dir / "missing").string(), (dir / "kept").string(), (dir / "notafile").string()};
	disks.values.setString("videos_folder", (dir / "missing").string());
	RestoredFolders gone = restoreFolders(disks);
	check(gone.paths.size() == 1 && gone.paths[0] == (dir / "kept").string() && gone.current.empty(),
	      "unreadable folders drop out and a missing current folder is dropped");
	disks.values.setString("videos_folder", (dir / "kept").string());
	RestoredFolders stayed = restoreFolders(disks);
	check(stayed.current == (dir / "kept").string(), "a readable current folder stays current");
	disks.folders.clear();
	disks.values.setString("videos_folder", (dir / "kept").string());
	RestoredFolders added = restoreFolders(disks);
	check(added.paths.size() == 1 && added.current == (dir / "kept").string(),
	      "a current folder that is not in the list is still opened");

	PerformanceSetup bad;
	check(!readSetup(dir / "nope.json", bad), "a missing file is left unread");
	std::ofstream(dir / "broken.json") << "{not json";
	check(!readSetup(dir / "broken.json", bad), "a broken file is left unread");

	SetupSaver saver(dir / "quiet.json", 0.25);
	PerformanceSetup quiet;
	saver.settle(quiet);
	quiet.values.setFloat("bpm", 140);
	saver.observe(quiet, 1.0);
	PerformanceSetup early;
	check(!readSetup(dir / "quiet.json", early), "a change is not written while the control is still moving");
	quiet.values.setFloat("bpm", 150);
	saver.observe(quiet, 1.125);
	saver.observe(quiet, 1.25);
	check(!readSetup(dir / "quiet.json", early), "another change restarts the wait");
	saver.observe(quiet, 1.5);
	PerformanceSetup settled;
	check(readSetup(dir / "quiet.json", settled) && near(settled.values.getFloat("bpm"), 150.f),
	      "the setup is written once the control has settled");
	quiet.values.setFloat("bpm", 160);
	saver.observe(quiet, 2.0);
	saver.flush();
	PerformanceSetup flushed;
	check(readSetup(dir / "quiet.json", flushed) && near(flushed.values.getFloat("bpm"), 160.f),
	      "quit writes a change that just happened");

	ParamBus snapParams;
	std::string snap = remoteSnapshot(snapParams, {}, {}, {}, true, "could not open \"clip\".mov");
	check(snap.find(R"("source_error":"could not open \"clip\".mov")") != std::string::npos,
	      "a load failure stays on the snapshot");
	check(remoteSnapshot(snapParams).find(R"("source_error":"")") != std::string::npos,
	      "the snapshot clears a load failure");

	std::filesystem::remove_all(dir);
	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
