#include "io/SourceController.hpp"
#include "io/DrawImage.hpp"
#include "io/FolderList.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>

#include "util/GlUtils.hpp"

SourceController::SourceController()
	: w(0), h(0), previewTex(0), previewFbo(0), frameTex(0), frameW(0), frameH(0), webcamActive(false),
	  fileActive(false), seenKind(-1), seenWebcamIndex(-1), spareW(0), spareH(0), job{0, false, {}, 0}, retire(false),
	  quit(false), commitRequested(false), committing(false), commitStartTime(0.0), commitNominalFps(0.0f),
	  commitFramesWritten(0), savedLoopSetting(true) {
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
	pages.shutdown();
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
	if (frameTex) {
		glDeleteTextures(1, &frameTex);
		frameTex = 0;
	}
	frameW = 0;
	frameH = 0;
	if (drawTex) {
		glDeleteTextures(1, &drawTex);
		drawTex = 0;
	}
	drawW = 0;
	drawH = 0;
}

bool SourceController::tickAndGrab(double nowSeconds, double dtSeconds, ParamBus& params) {
	bool presented = applyRequest(nowSeconds, params);
	int srcKind = params.getInt("source_kind", (int)SourceKind::Shader);
	bool justChoseStill = stillSourceCancelsLoad(srcKind, seenKind);
	if (justChoseStill && load.busy()) load.cancel();
	swappedLoad = presented || (!justChoseStill && swapInLoaded(params));
	if (!presented && swappedLoad && !dubOnSwap.empty()) {
		if (params.getInt("source_kind") == (int)SourceKind::File && params.getString("file_path") == dubOnSwap)
			beginCommit(nowSeconds, params, dubOnSwap);
		dubOnSwap.clear();
	}
	bool updatedPreviewTex = swappedLoad;
	ensureSource(params);

	if (srcKind == (int)SourceKind::Webcam) {
		ensureFrameTexture();
		int sw = 0, sh = 0;
		if (webcam.grab(frameTex, sw, sh)) {
			frameW = sw;
			frameH = sh;
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
		ensureFrameTexture();
		int sw = 0, sh = 0;
		if (fileSrc.grab(frameTex, sw, sh)) {
			frameW = sw;
			frameH = sh;
			updatedPreviewTex = true;
		}
	}

	maybeRescanFolder(params);
	maybeStartRandomCommit(nowSeconds, params);
	updateCommitProgress(updatedPreviewTex, params);

	return updatedPreviewTex;
}

void SourceController::triggerLoadRandomFile(ParamBus& params) {
	maybeRescanFolder(params);
	if (videoFiles.empty()) return;
	size_t idx = static_cast<size_t>(std::chrono::steady_clock::now().time_since_epoch().count()) % videoFiles.size();
	loadVideo(videoFiles[idx], params.getFloat("file_loop_start", 0.0f), params.getFloat("file_loop_length", 1.0f));
}

void SourceController::restartFileAtLoopStart() {
	if (fileActive) {
		fileSrc.restartAtLoopStart();
	}
}

void SourceController::loadPrevFile(ParamBus& params) {
	maybeRescanFolder(params);
	std::string pick = stepVideo(videoFiles, params.getString("file_path", ""), -1);
	if (!pick.empty())
		loadVideo(pick, params.getFloat("file_loop_start", 0.0f), params.getFloat("file_loop_length", 1.0f));
}

void SourceController::loadNextFile(ParamBus& params) {
	maybeRescanFolder(params);
	std::string pick = stepVideo(videoFiles, params.getString("file_path", ""), +1);
	if (!pick.empty())
		loadVideo(pick, params.getFloat("file_loop_start", 0.0f), params.getFloat("file_loop_length", 1.0f));
}

void SourceController::loadVideo(const std::string& path, float loopStart, float loopLength) {
	queueVideo(path, loopStart, loopLength, false);
}

void SourceController::queueDub(const std::string& path, float loopStart, float loopLength) {
	queueVideo(path, loopStart, loopLength, true);
}

void SourceController::primePage(const std::vector<std::string>& paths, float loopStart, float loopLength) {
	pages.setPage(paths, loopStart, loopLength);
}

void SourceController::queueVideo(const std::string& path, float loopStart, float loopLength, bool dub) {
	pendingVideo = {true, dub, path, loopStart, loopLength};
}

std::string SourceController::takeLoadFailure() {
	std::string reason = load.takeFailure();
	if (!reason.empty()) dubOnSwap.clear();
	return reason;
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
	cancelCommit(params);
	presentFrame(spareFrame, spareW, spareH);
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
		float loopStart = std::clamp(request.loopStart, 0.0f, 1.0f);
		float loopLen = std::clamp(request.loopLength, 0.0f, 1.0f);
		float loopEnd = std::clamp(loopStart + loopLen, loopStart + 0.001f, 1.0f);
		spareFile.setLoopRange(loopStart, loopEnd);
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

void SourceController::ensureFrameTexture() {
	if (frameTex) return;
	glGenTextures(1, &frameTex);
	glBindTexture(GL_TEXTURE_2D, frameTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
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
		if (!p.empty())
			loadVideo(p, params.getFloat("file_loop_start", 0.0f), params.getFloat("file_loop_length", 1.0f));
	}
}

void SourceController::maybeRescanFolder(ParamBus& params) {
	std::string folder = params.getString("videos_folder", "");
	if (folder != lastVideosFolder) showFolder(folder, scanVideos(folder), params);
}

void SourceController::showFolder(const std::string& path, const std::vector<std::string>& videos, ParamBus& params) {
	videoFiles = videos;
	lastVideosFolder = path;
	params.setString("videos_folder", path);
	params.setInt("videos_count", (int)videoFiles.size());
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
	bool readerEnded = !fileActive || fileSrc.ended();
	if (commitReachedEnd(false, params.getBool("file_loop", false), readerEnded)) cancelCommit(params);
}

bool SourceController::applyRequest(double nowSeconds, ParamBus& params) {
	if (!pendingVideo.on) return false;
	PendingVideo req = pendingVideo;
	pendingVideo.on = false;
	if (req.dub)
		dubOnSwap = req.path;
	else
		dubOnSwap.clear();
	if (!installReady(req.path, req.loopStart, req.loopLength, params)) {
		startLoad(load.requestVideo(req.path, req.loopStart, req.loopLength));
		return false;
	}
	if (req.dub) beginCommit(nowSeconds, params, req.path);
	dubOnSwap.clear();
	return true;
}

bool SourceController::installReady(const std::string& path, float loopStart, float loopLength, ParamBus& params) {
	AVFFileSource ready;
	std::vector<unsigned char> frame;
	int width = 0;
	int height = 0;
	if (!pages.take(path, loopStart, loopLength, ready, frame, width, height)) return false;
	load.cancel();
	cancelCommit(params);
	if (webcamActive) {
		webcam.stop();
		webcamActive = false;
	}
	if (fileActive) pages.retire(fileSrc);
	fileSrc.swap(ready);
	fileActive = true;
	seenKind = (int)SourceKind::File;
	params.setInt("source_kind", (int)SourceKind::File);
	params.setString("file_path", path);
	presentFrame(frame, width, height);
	return true;
}

void SourceController::beginCommit(double nowSeconds, ParamBus& params, const std::string& path) {
	if (!committing) savedLoopSetting = params.getBool("file_loop", true);
	params.setString("commit_current_file", path);
	params.setBool("commit_active", true);
	params.setBool("commit_random", false);
	params.setBool("file_loop", false);
	params.setBool("file_paused", false);
	if (fileActive) {
		fileSrc.setLoop(false);
		fileSrc.setPaused(false);
	}
	commitFramesWritten = 0;
	committing = true;
	commitStartTime = nowSeconds;
	commitNominalFps = fileActive ? fileSrc.nominalFps() : 0.0f;
	commitRequested = false;
}

void SourceController::cancelCommit(ParamBus& params) {
	if (!params.getBool("commit_active", false) && !committing) return;
	params.setBool("commit_active", false);
	params.setBool("commit_random", false);
	params.setString("commit_current_file", "");
	params.setBool("file_loop", savedLoopSetting);
	committing = false;
}

bool SourceController::presentDraw(const std::string& png) {
	std::vector<unsigned char> bgra;
	int width = 0;
	int height = 0;
	if (!decodeDrawImage(png, bgra, width, height)) return false;
	if (!drawTex) {
		glGenTextures(1, &drawTex);
		glBindTexture(GL_TEXTURE_2D, drawTex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	} else {
		glBindTexture(GL_TEXTURE_2D, drawTex);
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	drawW = width;
	drawH = height;
	return true;
}

void SourceController::presentFrame(const std::vector<unsigned char>& bgra, int width, int height) {
	if (width <= 0 || height <= 0 || bgra.empty()) return;
	ensureFrameTexture();
	glBindTexture(GL_TEXTURE_2D, frameTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	frameW = width;
	frameH = height;
}
