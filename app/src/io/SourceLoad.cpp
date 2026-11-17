#include "io/SourceLoad.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

LoadRequest SourceLoad::requestVideo(std::string p) {
	return request({0, false, std::move(p), 0});
}

LoadRequest SourceLoad::requestWebcam(int index) {
	return request({0, true, {}, index});
}

LoadRequest SourceLoad::request(LoadRequest next) {
	std::lock_guard<std::mutex> lock(mutex);
	next.id = ++lastId;
	pending = next;
	phase = Phase::Loading;
	reason.clear();
	return next;
}

void SourceLoad::cancel() {
	std::lock_guard<std::mutex> lock(mutex);
	++lastId;
	phase = Phase::Idle;
	pending = {};
	reason.clear();
}

bool SourceLoad::busy() const {
	std::lock_guard<std::mutex> lock(mutex);
	return phase != Phase::Idle;
}

uint64_t SourceLoad::inflight() const {
	std::lock_guard<std::mutex> lock(mutex);
	return phase == Phase::Idle ? 0 : pending.id;
}

void SourceLoad::resolve(uint64_t id, bool ok, std::string why) {
	std::lock_guard<std::mutex> lock(mutex);
	if (phase != Phase::Loading || id != pending.id) return;
	phase = ok ? Phase::Ready : Phase::Failed;
	reason = std::move(why);
}

bool SourceLoad::pollSwap(bool& outWebcam, std::string& outPath, int& outWebcamIndex) {
	std::lock_guard<std::mutex> lock(mutex);
	if (phase != Phase::Ready) return false;
	phase = Phase::Idle;
	webcam = pending.webcam;
	if (webcam)
		webcamIndex = pending.webcamIndex;
	else
		path = pending.path;
	outWebcam = webcam;
	outPath = path;
	outWebcamIndex = webcamIndex;
	return true;
}

std::string SourceLoad::takeFailure() {
	std::lock_guard<std::mutex> lock(mutex);
	if (phase != Phase::Failed) return {};
	phase = Phase::Idle;
	return std::exchange(reason, {});
}

bool SourceLoad::committedWebcam() const {
	std::lock_guard<std::mutex> lock(mutex);
	return webcam;
}

std::string SourceLoad::committedPath() const {
	std::lock_guard<std::mutex> lock(mutex);
	return path;
}

int SourceLoad::committedWebcamIndex() const {
	std::lock_guard<std::mutex> lock(mutex);
	return webcamIndex;
}

std::string stepVideo(const std::vector<std::string>& files, const std::string& current, int delta) {
	if (files.empty()) return {};
	long long n = static_cast<long long>(files.size());
	auto it = std::find(files.begin(), files.end(), current);
	long long from = it != files.end() ? it - files.begin() : (delta < 0 ? n : -1);
	return files[static_cast<size_t>(((from + delta) % n + n) % n)];
}

bool stillSourceCancelsLoad(int kind, int previousKind) {
	bool still = kind != 1 && kind != 3;
	return still && kind != previousKind;
}

std::string videoByName(const std::vector<std::string>& files, const std::string& name) {
	auto it = std::find_if(files.begin(), files.end(),
	                       [&](const std::string& f) { return std::filesystem::path(f).filename() == name; });
	return it != files.end() ? *it : std::string();
}
