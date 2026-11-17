#pragma once

#include <cstdio>
#include <string>
#include <string_view>

enum class ControlType { Bool, Float, Int, String };

struct Control {
	const char* name;
	ControlType type;
	float min;
	float max;
	bool writable;
};

// Every param the Web interface sees. Writable numbers are clamped to min..max; bools are not clamped.
inline constexpr Control kControls[] = {
	{"record_enabled", ControlType::Bool, 0, 0, true},
	{"record_fps", ControlType::Float, 1, 240, true},
	{"arm_record", ControlType::Bool, 0, 0, true},
	{"cue_active", ControlType::Bool, 0, 0, true},
	{"source_kind", ControlType::Int, 0, 3, true},
	{"video_bars", ControlType::Bool, 0, 0, true},
	{"video_aspect", ControlType::Bool, 0, 0, true},
	{"video_tx", ControlType::Float, -4, 4, true},
	{"video_ty", ControlType::Float, -4, 4, true},
	{"video_sx", ControlType::Float, 0.05f, 8, true},
	{"video_sy", ControlType::Float, 0.05f, 8, true},
	{"video_rot", ControlType::Float, -100, 100, true},
	{"webcam_index", ControlType::Int, 0, 16, true},
	{"file_loop", ControlType::Bool, 0, 0, true},
	{"file_paused", ControlType::Bool, 0, 0, true},
	{"file_run_mode", ControlType::Int, 0, 1, true},
	{"file_loop_start", ControlType::Float, 0, 0.999f, true},
	{"file_loop_length", ControlType::Float, 0, 1, true},
	{"gain_mix", ControlType::Float, 0, 1, true},
	{"record_blend_mode", ControlType::Int, 0, 11, true},
	{"record_blend_opacity", ControlType::Float, 0, 1, true},
	{"env_enabled", ControlType::Bool, 0, 0, true},
	{"env_attack", ControlType::Float, 0, 5, true},
	{"env_decay", ControlType::Float, 0, 5, true},
	{"env_sustain", ControlType::Float, 0, 1, true},
	{"env_release", ControlType::Float, 0, 5, true},
	{"fx_enabled", ControlType::Bool, 0, 0, true},
	{"fx_commit", ControlType::Bool, 0, 0, true},
	{"fx_mode", ControlType::Int, 0, 4, true},
	{"fx_param1", ControlType::Float, 0, 1, true},
	{"fx_param2", ControlType::Float, 0, 1, true},
	{"fx_param3", ControlType::Float, -0.05f, 0.05f, true},
	{"bpm", ControlType::Float, 40, 240, true},
	{"read_speed_mult", ControlType::Float, 0.25f, 4, true},
	{"tempo_coupled", ControlType::Bool, 0, 0, true},

	{"record_path", ControlType::String, 0, 0, false},
	{"show_ui", ControlType::Bool, 0, 0, false},
	{"show_preview", ControlType::Bool, 0, 0, false},
	{"preview_window", ControlType::Bool, 0, 0, false},
	{"show_playheads", ControlType::Bool, 0, 0, false},
	{"debug_preview_backbuffer", ControlType::Bool, 0, 0, false},
	{"link_enabled", ControlType::Bool, 0, 0, false},
	{"link_startstop", ControlType::Bool, 0, 0, false},
	{"link_playing", ControlType::Bool, 0, 0, false},
	{"preview_scale", ControlType::Float, 0, 0, false},
	{"quantum", ControlType::Float, 0, 0, false},
	{"videos_folder", ControlType::String, 0, 0, false},
	{"file_path", ControlType::String, 0, 0, false},
	{"videos_count", ControlType::Int, 0, 0, false},
	{"ipad_page", ControlType::Bool, 0, 0, false},
};

inline const Control* findControl(std::string_view name) {
	for (const Control& c : kControls)
		if (name == c.name) return &c;
	return nullptr;
}

// The table as one JSON object keyed by name; the page reads its ranges from this instead of keeping its own.
inline std::string controlSchemaJson() {
	const char* types[] = {"bool", "float", "int", "string"};
	std::string out = "{";
	for (const Control& c : kControls) {
		char buf[160];
		std::snprintf(buf, sizeof buf, "%s\"%s\":{\"type\":\"%s\",\"min\":%g,\"max\":%g,\"writable\":%s}",
		              out.size() > 1 ? "," : "", c.name, types[int(c.type)], c.min, c.max,
		              c.writable ? "true" : "false");
		out += buf;
	}
	return out + "}";
}
