#include "control/Instrument.hpp"

#include "control/ControlTable.hpp"
#include "io/FolderList.hpp"
#include "io/SourceLoad.hpp"
#include "io/VideoSource.hpp"
#include "ui/ParamBus.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

Outcome refused(const std::string& topic, const char* reason) {
	return {false, topic, reason};
}

const Control* controlOf(const std::string& name, ControlType type) {
	const Control* control = findControl(name);
	return control && control->type == type ? control : nullptr;
}

} // namespace

Instrument::Instrument(ParamBus& params, SourcePort& source, FolderList& folders, Tempo tempo)
	: params(params), source(source), folders(folders), tempo(std::move(tempo)) {}

Outcome Instrument::setSwitch(const std::string& name, bool on) {
	if (!controlOf(name, ControlType::Bool)) return refused(name, "bad value");
	if (name == "record_enabled" && on && params.getString("record_path").empty())
		return refused(name, "no export path");
	params.setBool(name, on);
	if (name == "video_aspect" && on) params.setFloat("video_sy", params.getFloat("video_sx"));
	if (name == "file_loop" || name == "file_paused") applyPlayback();
	return {};
}

Outcome Instrument::setNumber(const std::string& name, double value) {
	const Control* control = findControl(name);
	if (!control || (control->type != ControlType::Int && control->type != ControlType::Float) || !std::isfinite(value))
		return refused(name, "bad value");
	if (control->min < control->max) value = std::clamp(value, double(control->min), double(control->max));
	if (control->type == ControlType::Int) {
		int v = int(std::lround(value));
		if (name == "source_kind") {
			if (v == int(SourceKind::Webcam)) {
				source.loadWebcam(params.getInt("webcam_index"));
				return {};
			}
			params.setInt(name, v);
			if (v == int(SourceKind::File)) {
				std::string path = params.getString("file_path");
				if (source.status().kind != SourceKind::File && !path.empty())
					source.loadVideo(path, loopStart(), loopLength());
			} else {
				source.choose(static_cast<SourceKind>(v));
			}
			return {};
		}
		params.setInt(name, v);
		if (name == "webcam_index" && (params.getInt("source_kind") == int(SourceKind::Webcam) ||
		                               source.status().kind == SourceKind::Webcam))
			source.loadWebcam(v);
		return {};
	}
	float v = float(value);
	bool tempoChanged = name == "bpm" && v != params.getFloat("bpm");
	params.setFloat(name, v);
	if (tempoChanged && params.getBool("link_enabled") && tempo) tempo(v);
	if (name == "file_loop_start" || name == "file_loop_length") applyPlayback();
	return {};
}

Outcome Instrument::setText(const std::string& name, const std::string& text) {
	if (!controlOf(name, ControlType::String)) return refused(name, "bad value");
	if (name == "videos_folder") {
		if (!text.empty()) addFolder(text, scanVideos(text));
		return {};
	}
	params.setString(name, text);
	if (name == "file_path" && !text.empty()) source.loadVideo(text, loopStart(), loopLength());
	return {};
}

Outcome Instrument::nudge(const std::string& name, double delta) {
	if (name == "source_kind") return refused(name, "bad value");
	const Control* control = findControl(name);
	if (!control || (control->type != ControlType::Int && control->type != ControlType::Float))
		return refused(name, "bad value");
	double current = control->type == ControlType::Int ? double(params.getInt(name)) : double(params.getFloat(name));
	return setNumber(name, current + delta);
}

Outcome Instrument::toggle(const std::string& name) {
	if (!controlOf(name, ControlType::Bool)) return refused(name, "bad value");
	return setSwitch(name, !params.getBool(name));
}

void Instrument::stepSourceKind(int delta) {
	if (delta == 0) return;
	int kind = params.getInt("source_kind");
	if (delta > 0) {
		if (kind == int(SourceKind::Webcam) || kind == int(SourceKind::Draw)) return;
		setNumber("source_kind", kind + 1);
		return;
	}
	if (kind == int(SourceKind::Draw)) setNumber("source_kind", int(SourceKind::Black));
	else setNumber("source_kind", kind - 1);
}

void Instrument::panic() {
	params.setBool("arm_record", false);
	params.setBool("cue_active", false);
}

void Instrument::step(int delta) {
	std::string pick = stepVideo(currentVideos(), params.getString("file_path"), delta);
	if (!pick.empty()) loadVideo(pick);
}

void Instrument::prevVideo() {
	step(-1);
}

void Instrument::nextVideo() {
	step(+1);
}

void Instrument::randomVideo() {
	std::vector<std::string> videos = currentVideos();
	if (videos.empty()) return;
	size_t pick = size_t(std::chrono::steady_clock::now().time_since_epoch().count()) % videos.size();
	loadVideo(videos[pick]);
}

void Instrument::loadVideo(const std::string& path) {
	source.loadVideo(path, loopStart(), loopLength());
}

Outcome Instrument::loadVideoNamed(const std::string& name) {
	std::string path = videoByName(currentVideos(), name);
	if (path.empty()) return refused("source", "no such video");
	loadVideo(path);
	return {};
}

Outcome Instrument::dubVideoNamed(const std::string& name) {
	std::string path = videoByName(currentVideos(), name);
	if (path.empty()) return refused("source", "no such video");
	source.queueDub(path, loopStart(), loopLength());
	return {};
}

void Instrument::primePage(const std::vector<std::string>& names) {
	source.primePage(videosByName(currentVideos(), names), loopStart(), loopLength());
}

Outcome Instrument::chooseFolder(const std::string& path) {
	if (!folders.select(path)) return refused("folder", "no such folder");
	params.setString("videos_folder", path);
	params.setInt("videos_count", int(folders.current()->videos.size()));
	return {};
}

void Instrument::addFolder(const std::string& path, const std::vector<std::string>& videos) {
	folders.add({path, videos});
	chooseFolder(path);
}

void Instrument::chooseWebcam() {
	source.loadWebcam(params.getInt("webcam_index"));
}

void Instrument::record(const SourceStatus& status) {
	if (!status.swapped) return;
	params.setInt("source_kind", int(status.kind));
	params.setString("file_path", status.path);
	if (status.kind == SourceKind::Webcam) params.setInt("webcam_index", status.webcamIndex);
}

void Instrument::restartVideo() {
	if (params.getInt("file_run_mode") == 1 && params.getInt("source_kind") == int(SourceKind::File))
		source.restartFileAtLoopStart();
}

void Instrument::restoreSource() {
	applyPlayback();
	int kind = params.getInt("source_kind");
	if (kind == int(SourceKind::File)) {
		std::string path = params.getString("file_path");
		if (!path.empty()) source.loadVideo(path, loopStart(), loopLength());
	} else if (kind == int(SourceKind::Webcam)) {
		source.loadWebcam(params.getInt("webcam_index"));
	} else {
		source.choose(static_cast<SourceKind>(kind));
	}
}

void Instrument::applyPlayback() {
	source.setPlayback(params.getBool("file_loop"), params.getBool("file_paused"), loopStart(), loopLength());
}

float Instrument::loopStart() const {
	return params.getFloat("file_loop_start");
}

float Instrument::loopLength() const {
	return params.getFloat("file_loop_length");
}

std::vector<std::string> Instrument::currentVideos() const {
	const PreparedFolder* folder = folders.current();
	return folder ? folder->videos : std::vector<std::string>{};
}
