#pragma once

#include <cstdio>
#include <string>
#include <string_view>

#include "util/Repeat.hpp"

enum class ControlType { Bool, Float, Int, String };

// Whether the Web interface sees a control, and whether it may set it.
enum class Web { Hidden, Read, Write };

// Session: not in the saved setup. Saved: written to the setup and restored at launch.
// StartsOff: not saved, and put back to its default when a setup is restored, so a relaunch does not write
// by itself (ADR-0003).
enum class Keep { Session, Saved, StartsOff };

struct Control {
	const char* name;
	ControlType type;
	float def; // a Bool is 0 or 1; a String starts empty
	float min;
	float max;
	Web web;
	Keep keep;
};

// Every control, once. A fresh ParamBus holds these defaults. Setting a number through the instrument clamps
// it to min..max; a min equal to max means no range. Bools are not clamped.
inline constexpr Control kControls[] = {
	{"record_enabled", ControlType::Bool, 0, 0, 0, Web::Write, Keep::StartsOff},
	{"record_fps", ControlType::Float, 60, 1, 240, Web::Write, Keep::Saved},
	{"arm_record", ControlType::Bool, 0, 0, 0, Web::Write, Keep::StartsOff},
	{"cue_active", ControlType::Bool, 0, 0, 0, Web::Write, Keep::StartsOff},
	{"source_kind", ControlType::Int, 0, 0, 3, Web::Write, Keep::Saved},
	{"video_bars", ControlType::Bool, 1, 0, 0, Web::Write, Keep::Saved},
	{"video_aspect", ControlType::Bool, 1, 0, 0, Web::Write, Keep::Saved},
	{"video_tx", ControlType::Float, 0, -4, 4, Web::Write, Keep::Saved},
	{"video_ty", ControlType::Float, 0, -4, 4, Web::Write, Keep::Saved},
	{"video_sx", ControlType::Float, 1, 0.05f, 8, Web::Write, Keep::Saved},
	{"video_sy", ControlType::Float, 1, 0.05f, 8, Web::Write, Keep::Saved},
	{"video_rot", ControlType::Float, 0, -100, 100, Web::Write, Keep::Saved},
	{"webcam_index", ControlType::Int, 0, 0, 16, Web::Write, Keep::Saved},
	{"file_loop", ControlType::Bool, 1, 0, 0, Web::Write, Keep::Saved},
	{"file_paused", ControlType::Bool, 0, 0, 0, Web::Write, Keep::Saved},
	{"file_run_mode", ControlType::Int, 0, 0, 1, Web::Write, Keep::Saved},
	{"file_loop_start", ControlType::Float, 0, 0, 0.999f, Web::Write, Keep::Saved},
	{"file_loop_length", ControlType::Float, 1, 0, 1, Web::Write, Keep::Saved},
	{"gain_mix", ControlType::Float, 1, 0, 1, Web::Write, Keep::Saved},
	{"record_blend_mode", ControlType::Int, 0, 0, 11, Web::Write, Keep::Saved},
	{"record_blend_opacity", ControlType::Float, 1, 0, 1, Web::Write, Keep::Saved},
	{"env_enabled", ControlType::Bool, 1, 0, 0, Web::Write, Keep::Saved},
	{"env_attack", ControlType::Float, 0.05f, 0, 5, Web::Write, Keep::Saved},
	{"env_decay", ControlType::Float, 0.20f, 0, 5, Web::Write, Keep::Saved},
	{"env_sustain", ControlType::Float, 1, 0, 1, Web::Write, Keep::Saved},
	{"env_release", ControlType::Float, 0.30f, 0, 5, Web::Write, Keep::Saved},
	{"fx_enabled", ControlType::Bool, 0, 0, 0, Web::Write, Keep::Saved},
	{"fx_commit", ControlType::Bool, 0, 0, 0, Web::Write, Keep::Saved},
	{"fx_mode", ControlType::Int, 0, 0, kGridFxMode, Web::Write, Keep::Saved},
	{"fx_param1", ControlType::Float, 0.8f, 0, 1, Web::Write, Keep::Saved},
	{"fx_param2", ControlType::Float, 0.985f, 0, 1, Web::Write, Keep::Saved},
	{"fx_param3", ControlType::Float, 0, -0.05f, 0.05f, Web::Write, Keep::Saved},
	{"fx_count", ControlType::Int, 4, 0, kRepeatMaxCopies, Web::Write, Keep::Saved},
	{"fx_repeat_blend", ControlType::Int, 6, 0, 11, Web::Write, Keep::Saved},
	{"source_fx_enabled", ControlType::Bool, 0, 0, 0, Web::Write, Keep::Saved},
	{"source_fx_mode", ControlType::Int, kRepeatFxMode, 0, kGridFxMode, Web::Write, Keep::Saved},
	{"source_fx_param1", ControlType::Float, 0.12f, 0, 1, Web::Write, Keep::Saved},
	{"source_fx_param2", ControlType::Float, 0, 0, 1, Web::Write, Keep::Saved},
	{"source_fx_param3", ControlType::Float, 0, -0.05f, 0.05f, Web::Write, Keep::Saved},
	{"source_fx_count", ControlType::Int, 4, 0, kRepeatMaxCopies, Web::Write, Keep::Saved},
	{"source_fx_repeat_blend", ControlType::Int, 6, 0, 11, Web::Write, Keep::Saved},
	{"bpm", ControlType::Float, 120, 40, 240, Web::Write, Keep::Saved},
	{"read_speed_mult", ControlType::Float, 1, 0.25f, 4, Web::Write, Keep::Saved},
	{"tempo_coupled", ControlType::Bool, 1, 0, 0, Web::Write, Keep::Saved},

	{"record_path", ControlType::String, 0, 0, 0, Web::Read, Keep::Saved},
	{"show_ui", ControlType::Bool, 1, 0, 0, Web::Read, Keep::Session},
	{"show_preview", ControlType::Bool, 1, 0, 0, Web::Read, Keep::Session},
	{"preview_window", ControlType::Bool, 0, 0, 0, Web::Read, Keep::Session},
	{"show_playheads", ControlType::Bool, 1, 0, 0, Web::Read, Keep::Session},
	{"debug_preview_backbuffer", ControlType::Bool, 0, 0, 0, Web::Read, Keep::Session},
	{"link_enabled", ControlType::Bool, 0, 0, 0, Web::Read, Keep::Session},
	{"link_startstop", ControlType::Bool, 0, 0, 0, Web::Read, Keep::Session},
	{"link_playing", ControlType::Bool, 0, 0, 0, Web::Read, Keep::Session},
	{"preview_scale", ControlType::Float, 0.3f, 0.1f, 0.6f, Web::Read, Keep::Session},
	{"quantum", ControlType::Float, 4, 1, 16, Web::Read, Keep::Session},
	{"videos_folder", ControlType::String, 0, 0, 0, Web::Read, Keep::Saved},
	{"file_path", ControlType::String, 0, 0, 0, Web::Read, Keep::Saved},
	{"videos_count", ControlType::Int, 0, 0, 0, Web::Read, Keep::Session},
	{"ipad_page", ControlType::Bool, 0, 0, 0, Web::Read, Keep::Saved},

	{"ipad_token", ControlType::Bool, 1, 0, 0, Web::Hidden, Keep::Session},
	{"silence_ms", ControlType::Float, 300, 50, 2000, Web::Hidden, Keep::Session},
};

inline const Control* findControl(std::string_view name) {
	for (const Control& c : kControls)
		if (name == c.name) return &c;
	return nullptr;
}

// The controls the Web interface sees, as one JSON object keyed by name. The page reads its ranges and
// defaults from this instead of keeping its own.
inline std::string controlSchemaJson() {
	const char* types[] = {"bool", "float", "int", "string"};
	std::string out = "{";
	for (const Control& c : kControls) {
		if (c.web == Web::Hidden) continue;
		char buf[200];
		std::snprintf(buf, sizeof buf, "%s\"%s\":{\"type\":\"%s\",\"min\":%g,\"max\":%g,\"def\":%g,\"writable\":%s}",
		              out.size() > 1 ? "," : "", c.name, types[int(c.type)], c.min, c.max, c.def,
		              c.web == Web::Write ? "true" : "false");
		out += buf;
	}
	return out + "}";
}
