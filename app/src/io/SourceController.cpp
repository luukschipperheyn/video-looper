#include "io/SourceController.hpp"

#include <filesystem>

#include "util/GlUtils.hpp"

SourceController::SourceController()
	: w(0), h(0), previewTex(0), previewFbo(0),
	  webcamActive(false), currentWebcamIndex(-1), fileActive(false),
	  commitRequested(false), committing(false), commitStartTime(0.0),
	  commitNominalFps(0.0f), commitFramesWritten(0), savedLoopSetting(true) {}

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
	if (webcamActive) { webcam.stop(); webcamActive = false; }
	if (fileActive) { fileSrc.stop(); fileActive = false; }
	destroyPreview();
}

bool SourceController::tickAndGrab(double nowSeconds, double dtSeconds, ParamBus& params) {
	ensureSource(params);
	bool updatedPreviewTex = false;

    int srcKind = params.getInt("source_kind", (int)SourceKind::Shader);
    if (srcKind == (int)SourceKind::Webcam) {
		int sw=0, sh=0;
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
        int sw=0, sh=0;
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

static size_t wrapIndex(long long value, size_t size) {
    if (size == 0) return 0;
    long long mod = value % static_cast<long long>(size);
    if (mod < 0) mod += static_cast<long long>(size);
    return static_cast<size_t>(mod);
}

void SourceController::loadPrevFile(ParamBus& params) {
    maybeRescanFolder(params);
    if (videoFiles.empty()) return;
    std::string current = params.getString("file_path", "");
    long long idx = -1;
    for (size_t i = 0; i < videoFiles.size(); ++i) {
        if (videoFiles[i] == current) { idx = static_cast<long long>(i); break; }
    }
    size_t nextIdx = wrapIndex((idx == -1 ? static_cast<long long>(videoFiles.size()) - 1 : idx - 1), videoFiles.size());
    std::string pick = videoFiles[nextIdx];
    params.setInt("source_kind", (int)SourceKind::File);
    params.setString("file_path", pick);
    params.setBool("commit_active", false);
    params.setString("commit_current_file", "");
    if (fileActive) { fileSrc.stop(); fileActive = false; }
    fileActive = fileSrc.startFile(pick);
}

void SourceController::loadNextFile(ParamBus& params) {
    maybeRescanFolder(params);
    if (videoFiles.empty()) return;
    std::string current = params.getString("file_path", "");
    long long idx = -1;
    for (size_t i = 0; i < videoFiles.size(); ++i) {
        if (videoFiles[i] == current) { idx = static_cast<long long>(i); break; }
    }
    size_t nextIdx = wrapIndex((idx == -1 ? 0 : idx + 1), videoFiles.size());
    std::string pick = videoFiles[nextIdx];
    params.setInt("source_kind", (int)SourceKind::File);
    params.setString("file_path", pick);
    params.setBool("commit_active", false);
    params.setString("commit_current_file", "");
    if (fileActive) { fileSrc.stop(); fileActive = false; }
    fileActive = fileSrc.startFile(pick);
}

void SourceController::allocatePreview(int width, int height) {
	previewTex = createTexture2D(width, height);
	previewFbo = createFbo(previewTex);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SourceController::destroyPreview() {
	if (previewFbo) { glDeleteFramebuffers(1, &previewFbo); previewFbo = 0; }
	if (previewTex) { glDeleteTextures(1, &previewTex); previewTex = 0; }
}

void SourceController::ensureSource(ParamBus& params) {
    int srcKind = params.getInt("source_kind", (int)SourceKind::Shader);
    if (srcKind == (int)SourceKind::Webcam) {
		int desiredIndex = params.getInt("webcam_index", 0);
		bool needsReinit = (!webcamActive) || (currentWebcamIndex != desiredIndex);
		if (needsReinit) {
			if (webcamActive) { webcam.stop(); webcamActive = false; }
			printf("(Re)starting webcam %d\n", desiredIndex);
			webcamActive = webcam.start(desiredIndex);
			currentWebcamIndex = desiredIndex;
		}
		if (fileActive) { fileSrc.stop(); fileActive = false; }
	} else if (srcKind == (int)SourceKind::File) {
		if (!fileActive) {
			std::string p = params.getString("file_path", "");
			if (!p.empty()) fileActive = fileSrc.startFile(p);
		}
		if (webcamActive) { webcam.stop(); webcamActive = false; }
    } else {
		if (webcamActive) { webcam.stop(); webcamActive = false; }
		if (fileActive) { fileSrc.stop(); fileActive = false; }
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
			for (auto& c : ext) c = (char)std::tolower(c);
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

	if (fileActive) { fileSrc.stop(); fileActive = false; }
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


