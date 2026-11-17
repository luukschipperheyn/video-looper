#pragma once

#include "io/FolderList.hpp"
#include "ui/ParamBus.hpp"

#include <filesystem>
#include <string>
#include <vector>

// The performance setup that comes back at launch. The Loop's frames are not part of it.
struct PerformanceSetup {
	int record_blend_mode = 0;
	float record_blend_opacity = 1;
	float gain_mix = 1;
	bool env_enabled = true;
	float env_attack = 0.05f;
	float env_decay = 0.20f;
	float env_sustain = 1;
	float env_release = 0.30f;
	bool fx_enabled = false;
	bool fx_commit = false;
	int fx_mode = 0;
	float fx_param1 = 0.8f;
	float fx_param2 = 0.985f;
	float fx_param3 = 0;
	int source_kind = 0;
	float video_tx = 0;
	float video_ty = 0;
	float video_sx = 1;
	float video_sy = 1;
	float video_rot = 0;
	bool video_bars = true;
	bool video_aspect = true;
	int webcam_index = 0;
	bool file_loop = true;
	bool file_paused = false;
	int file_run_mode = 0;
	float file_loop_start = 0;
	float file_loop_length = 1;
	std::string file_path;
	std::vector<std::string> folders;
	std::string videos_folder;
	float bpm = 120;
	float read_speed_mult = 1;
	bool tempo_coupled = true;
	bool ipad_page = false;
	std::string record_path;
	float record_fps = 60;
};

// paths keeps the saved folders that are still directories, in order.
// current is the saved Videos folder when it can still be read.
struct RestoredFolders {
	std::vector<std::string> paths;
	std::string current;
};

PerformanceSetup captureSetup(const ParamBus& params, const FolderList& folders);
// Latch, latched Cue, and Export are left off. Params outside the setup are left as they are.
void applySetup(ParamBus& params, const PerformanceSetup& setup);
bool writeSetup(const std::filesystem::path& path, const PerformanceSetup& setup);
bool readSetup(const std::filesystem::path& path, PerformanceSetup& setup);
RestoredFolders restoreFolders(const PerformanceSetup& setup);

// Writes once the setup has stopped changing, which is a control being released.
class SetupSaver {
public:
	SetupSaver(std::filesystem::path path, double quietSeconds);
	void settle(const PerformanceSetup& setup);
	void observe(const PerformanceSetup& setup, double nowSeconds);
	void flush();

private:
	std::filesystem::path file;
	double quiet;
	std::string saved;
	std::string pending;
	double since = 0;
	bool waiting = false;
};
