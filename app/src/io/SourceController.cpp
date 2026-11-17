#include "io/SourceController.hpp"

#include <chrono>
#include <filesystem>

#include "util/GlUtils.hpp"

SourceController::SourceController()
	: w(0), h(0), previewTex(0), previewFbo(0), webcamActive(false), fileActive(false), seenKind(-1),
	  seenWebcamIndex(-1), spareW(0), spareH(0), job{0, false, {}, 0}, retire(false), quit(false),
	  commitRequested(false), committing(false), commitStartTime(0.0), commitNominalFps(0.0f), commitFramesWritten(0),
	  savedLoopSetting(true) {
	worker = std::thread(&SourceController::runLoads, this);
}

SourceController::~SourceController() {
	shutdown();
}

void SourceController::init(int width, int height) {
	w = width;
	h = height;
	allocatePreview(w, h);
}

void SourceController::resize(int width, int height) {
	if (width == w && height == h) return;
	w = width;
	h = height;
	destroyPreview();
	allocatePreview(w, h);
}

void SourceController::shutdown() {
	if (worker.joinable()) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			quit = true;
		}
		jobReady.notify_one();
		worker.join();
	}
	if (webcamActive) {
		webcam.stop();
		webcamActive = false;
	}
	if (fileActive) {
		fileSrc.stop();
		fileActive = false;
	}
	destroyPreview();
}

bool SourceController::tickAndGrab(double nowSeconds, double dtSeconds, ParamBus& params) {
	int srcKind = params.getInt("source_kind", (int)SourceKind::Shader);
	bool justChoseStill = stillSourceCancelsLoad(srcKind, seenKind);
	if (justChoseStill && load.busy()) load.cancel();
	bool updatedPreviewTex = !justChoseStill && swapInLoaded(params);
	ensureSource(params);

	if (srcKind == (int)SourceKind::Webcam) {
		int sw = 0, sh = 0;
		if (webcam.grab(previewTex, sw, sh)) {
			updatedPreviewTex = true;
		}
	} else if (srcKind == (int)SourceKind::File) {
		fileSrc.setLoop(params.getBool("file_loop", true));
		fileSrc.setPaused(params.getBool("file_paused", false));
		// Apply normalized loop range using start + length
		float loopStart = params.getFloat("file_loop_start", 0.0f);
		float loopLen = params.getFloat("file_loop_length", 1.0f);
		loopStart = std::clamp(loopStart, 0.0f, 1.0f);
		loopLen = std::clamp(loopLen, 0.0f, 1.0f);
		float loopEnd = std::clamp(loopStart + loopLen, loopStart + 0.001f, 1.0f);
		fileSrc.setLoopRange(loopStart, loopEnd);
		fileSrc.tick(dtSeconds);
		int sw = 0, sh = 0;
		if (fileSrc.grab(previewTex, sw, sh)) {
			updatedPreviewTex = true;
		}
	}

	maybeRescanFolder(params);
	maybeStartRandomCommit(nowSeconds, params);
	updateCommitProgress(updatedPreviewTex, params);

	return updatedPreviewTex;
}

void SourceController::triggerLoadRandomFile() {
	commitRequested = true;
}

void SourceController::restartFileAtLoopStart() {
	if (fileActive) {
		fileSrc.restartAtLoopStart();
	}
}

void SourceController::loadPrevFile(ParamBus& params) {
	maybeRescanFolder(params);
	std::string pick = stepVideo(videoFiles, params.getString("file_path", ""), -1);
	if (!pick.empty()) loadVideo(pick);
}

void SourceController::loadNextFile(ParamBus& params) {
	maybeRescanFolder(params);
	std::string pick = stepVideo(videoFiles, params.getString("file_path", ""), +1);
	if (!pick.empty()) loadVideo(pick);
}

void SourceController::loadVideo(const std::string& path) {
	startLoad(load.requestVideo(path));
}

void SourceController::loadWebcam(int index) {
	startLoad(load.requestWebcam(index));
}

void SourceController::startLoad(const LoadRequest& request) {
	{
		std::lock_guard<std::mutex> lock(jobMutex);
		job = request;
	}
	jobReady.notify_one();
}

// Swaps a loaded Source in one step: the spares become live, and the previous live sources go to the worker to stop.
bool SourceController::swapInLoaded(ParamBus& params) {
	bool isWebcam = false;
	std::string path;
	int index = 0;
	if (!load.pollSwap(isWebcam, path, index)) return false;
	webcam.swap(spareWebcam);
	fileSrc.swap(spareFile);
	webcamActive = isWebcam;
	fileActive = !isWebcam;
	params.setInt("source_kind", (int)(isWebcam ? SourceKind::Webcam : SourceKind::File));
	params.setString("file_path", path);
	if (isWebcam) {
		params.setInt("webcam_index", index);
		seenWebcamIndex = index;
	}
	params.setBool("commit_active", false);
	params.setString("commit_current_file", "");
	glBindTexture(GL_TEXTURE_2D, previewTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, spareW, spareH, 0, GL_BGRA, GL_UNSIGNED_BYTE, spareFrame.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	{
		std::lock_guard<std::mutex> lock(jobMutex);
		retire = true;
	}
	jobReady.notify_one();
	return true;
}

void SourceController::runLoads() {
	uint64_t done = 0;
	for (;;) {
		LoadRequest request;
		{
			std::unique_lock<std::mutex> lock(jobMutex);
			jobReady.wait(lock, [&] { return quit || retire || job.id != done; });
			if (quit) return;
			retire = false;
			request = job;
		}
		spareWebcam.stop();
		spareFile.stop();
		if (request.id == done) continue;
		done = request.id;
		std::string reason = openSpare(request);
		if (!reason.empty() || load.inflight() != request.id) {
			spareWebcam.stop();
			spareFile.stop();
		}
		load.resolve(request.id, reason.empty(), reason);
	}
}

// Opens request into a spare and copies its first frame. Returns why it failed, or empty on success.
std::string SourceController::openSpare(const LoadRequest& request) {
	if (!request.webcam) {
		std::string name = std::filesystem::path(request.path).filename().string();
		if (!spareFile.startFile(request.path)) return "could not open " + name;
		if (!spareFile.copyFrame(spareFrame, spareW, spareH)) return "no frame in " + name;
		return {};
	}
	if (!spareWebcam.start(request.webcamIndex)) return "could not open webcam";
	for (int i = 0; i < 300; ++i) {
		if (spareWebcam.copyFrame(spareFrame, spareW, spareH)) return {};
		if (quit || load.inflight() != request.id) return "cancelled";
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return "no frame from webcam";
}

void SourceController::allocatePreview(int width, int height) {
	previewTex = createTexture2D(width, height);
	previewFbo = createFbo(previewTex);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SourceController::destroyPreview() {
	if (previewFbo) {
		glDeleteFramebuffers(1, &previewFbo);
		previewFbo = 0;
	}
	if (previewTex) {
		glDeleteTextures(1, &previewTex);
		previewTex = 0;
	}
}

// The live sources change only in swapInLoaded. A param edge that names a Source that is not live starts a load;
// reacting to edges only keeps a failed load from retrying every frame.
void SourceController::ensureSource(ParamBus& params) {
	int kind = params.getInt("source_kind", (int)SourceKind::Shader);
	int index = params.getInt("webcam_index", 0);
	bool kindChanged = kind != seenKind;
	bool indexChanged = index != seenWebcamIndex;
	seenKind = kind;
	seenWebcamIndex = index;
	if (kindChanged && kind != (int)SourceKind::File && kind != (int)SourceKind::Webcam) {
		if (webcamActive) {
			webcam.stop();
			webcamActive = false;
		}
		if (fileActive) {
			fileSrc.stop();
			fileActive = false;
		}
	} else if (indexChanged && webcamActive) {
		loadWebcam(index);
	} else if (kindChanged && kind == (int)SourceKind::File && !fileActive) {
		std::string p = params.getString("file_path", "");
		if (!p.empty()) loadVideo(p);
	}
}

void SourceController::maybeRescanFolder(ParamBus& params) {
	std::string folder = params.getString("videos_folder", "");
	int existingCount = params.getInt("videos_count", 0);
	bool needsScan = (folder != lastVideosFolder);
	if (!needsScan) {
		// Heuristic: UI toggled some input -> we can force a rescan when count is 0 but a folder exists
		needsScan = (existingCount == 0 && !folder.empty());
	}
	if (!needsScan) return;

	videoFiles.clear();
	std::error_code ec;
	if (!folder.empty()) {
		for (auto const& de : std::filesystem::directory_iterator(folder, ec)) {
			if (ec) break;
			if (!de.is_regular_file()) continue;
			auto p = de.path();
			std::string ext = p.extension().string();
			for (auto& c : ext)
				c = (char)std::tolower(c);
			if (ext == ".mp4" || ext == ".mov" || ext == ".m4v" || ext == ".mkv" || ext == ".avi") {
				videoFiles.push_back(p.string());
			}
		}
	}
	params.setInt("videos_count", (int)videoFiles.size());
	lastVideosFolder = folder;
}

void SourceController::maybeStartRandomCommit(double nowSeconds, ParamBus& params) {
	if (params.getBool("commit_active", false)) return;
	bool trigger = commitRequested || params.getBool("commit_random", false);
	if (!trigger) return;
	if (videoFiles.empty()) return;

	size_t idx = static_cast<size_t>(nowSeconds * 1000000.0) % videoFiles.size();
	std::string pick = videoFiles[idx];
	params.setString("commit_current_file", pick);
	params.setBool("commit_active", true);
	params.setBool("commit_random", false);

	params.setInt("source_kind", (int)SourceKind::File);
	params.setString("file_path", pick);
	savedLoopSetting = params.getBool("file_loop", true);
	params.setBool("file_loop", false);
	params.setBool("file_paused", false);

	if (fileActive) {
		fileSrc.stop();
		fileActive = false;
	}
	fileActive = fileSrc.startFile(pick);
	commitFramesWritten = 0;
	committing = true;
	commitStartTime = nowSeconds;
	commitNominalFps = fileSrc.nominalFps();
	commitRequested = false;
}

void SourceController::updateCommitProgress(bool updatedPreviewTex, ParamBus& params) {
	if (!params.getBool("commit_active", false)) return;
	if (updatedPreviewTex) {
		++commitFramesWritten;
		return;
	}
	if (!params.getBool("file_loop", false)) {
		params.setBool("commit_active", false);
		params.setBool("commit_random", false);
		params.setBool("file_loop", savedLoopSetting);
		params.setString("commit_current_file", "");
		committing = false;
	}
}
