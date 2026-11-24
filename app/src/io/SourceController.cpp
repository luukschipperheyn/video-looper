#include "io/SourceController.hpp"
#include "io/FolderList.hpp"

#include <chrono>
#include <filesystem>
#include <utility>

SourceController::SourceController(PicturePort& picturePort, DecoderFactory factory)
	: picture(picturePort), decoders(std::move(factory)), webcam(decoders.camera()), fileSrc(decoders.video()),
	  spareWebcam(decoders.camera()), spareFile(decoders.video()), pages(decoders.video) {
	worker = std::thread(&SourceController::runLoads, this);
}

SourceController::~SourceController() {
	shutdown();
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
		webcam->stop();
		webcamActive = false;
	}
	if (fileActive) {
		fileSrc->stop();
		fileActive = false;
	}
	frameW = 0;
	frameH = 0;
}

bool SourceController::tick(double dtSeconds, ParamBus& params) {
	bool presented = applyRequest(params);
	int srcKind = params.getInt("source_kind");
	// The first tick has no earlier Source kind, so it is not a choice.
	bool justChoseStill = seenKind >= 0 && stillSourceCancelsLoad(srcKind, seenKind);
	if (justChoseStill && load.busy()) load.cancel();
	swappedLoad = presented || (!justChoseStill && swapInLoaded(params));
	if (!presented && swappedLoad && !dubOnSwap.empty()) {
		if (params.getInt("source_kind") == (int)SourceKind::File && params.getString("file_path") == dubOnSwap)
			beginCommit(dubOnSwap);
		dubOnSwap.clear();
	}
	bool newPicture = swappedLoad;
	ensureSource(params);

	bool looping = !commitOn && params.getBool("file_loop");
	bool paused = !commitOn && params.getBool("file_paused");
	if (srcKind == (int)SourceKind::Webcam) {
		int sw = 0, sh = 0;
		if (webcam->grab(picture.pictureTexture(), sw, sh)) {
			frameW = sw;
			frameH = sh;
			newPicture = true;
		}
	} else if (srcKind == (int)SourceKind::File) {
		fileSrc->setLoop(looping);
		fileSrc->setPaused(paused);
		LoopRange range = loopRange(params.getFloat("file_loop_start"), params.getFloat("file_loop_length"));
		fileSrc->setLoopRange(range.start, range.end);
		fileSrc->tick(dtSeconds);
		int sw = 0, sh = 0;
		if (fileSrc->grab(picture.pictureTexture(), sw, sh)) {
			frameW = sw;
			frameH = sh;
			newPicture = true;
		}
	}

	maybeRescanFolder(params);

	if (commitOn && !newPicture) {
		bool readerEnded = !fileActive || fileSrc->ended();
		if (commitReachedEnd(false, looping, readerEnded)) cancelCommit();
	}
	return newPicture;
}

void SourceController::restartFileAtLoopStart() {
	if (fileActive) fileSrc->restartAtLoopStart();
}

void SourceController::loadVideo(const std::string& path, float loopStart, float loopLength) {
	pendingVideo = {true, false, path, loopStart, loopLength};
}

void SourceController::queueDub(const std::string& path, float loopStart, float loopLength) {
	pendingVideo = {true, true, path, loopStart, loopLength};
}

void SourceController::primePage(const std::vector<std::string>& paths, float loopStart, float loopLength) {
	pages.setPage(paths, loopStart, loopLength);
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
	std::swap(webcam, spareWebcam);
	std::swap(fileSrc, spareFile);
	webcamActive = isWebcam;
	fileActive = !isWebcam;
	params.setInt("source_kind", (int)(isWebcam ? SourceKind::Webcam : SourceKind::File));
	params.setString("file_path", path);
	if (isWebcam) {
		params.setInt("webcam_index", index);
		seenWebcamIndex = index;
	}
	cancelCommit();
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
		spareWebcam->stop();
		spareFile->stop();
		if (request.id == done) continue;
		done = request.id;
		std::string reason = openSpare(request);
		if (!reason.empty() || load.inflight() != request.id) {
			spareWebcam->stop();
			spareFile->stop();
		}
		load.resolve(request.id, reason.empty(), reason);
	}
}

// Opens request into a spare and copies its first frame. Returns why it failed, or empty on success.
std::string SourceController::openSpare(const LoadRequest& request) {
	if (!request.webcam) {
		std::string name = std::filesystem::path(request.path).filename().string();
		if (!spareFile->startFile(request.path)) return "could not open " + name;
		LoopRange range = loopRange(request.loopStart, request.loopLength);
		spareFile->setLoopRange(range.start, range.end);
		if (!spareFile->copyFrame(spareFrame, spareW, spareH)) return "no frame in " + name;
		return {};
	}
	if (!spareWebcam->start(request.webcamIndex)) return "could not open webcam";
	for (int i = 0; i < 300; ++i) {
		if (spareWebcam->copyFrame(spareFrame, spareW, spareH)) return {};
		if (quit || load.inflight() != request.id) return "cancelled";
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return "no frame from webcam";
}

// The live sources change only when a load swaps in. A param edge that names a Source that is not live starts a
// load; reacting to edges only keeps a failed load from retrying every frame.
void SourceController::ensureSource(ParamBus& params) {
	int kind = params.getInt("source_kind");
	int index = params.getInt("webcam_index");
	bool kindChanged = kind != seenKind;
	bool indexChanged = index != seenWebcamIndex;
	seenKind = kind;
	seenWebcamIndex = index;
	if (kindChanged && kind != (int)SourceKind::File && kind != (int)SourceKind::Webcam) {
		if (webcamActive) {
			webcam->stop();
			webcamActive = false;
		}
		if (fileActive) {
			fileSrc->stop();
			fileActive = false;
		}
	} else if (indexChanged && webcamActive) {
		loadWebcam(index);
	} else if (kindChanged && kind == (int)SourceKind::File && !fileActive) {
		std::string p = params.getString("file_path");
		if (!p.empty()) loadVideo(p, params.getFloat("file_loop_start"), params.getFloat("file_loop_length"));
	}
}

void SourceController::maybeRescanFolder(ParamBus& params) {
	std::string folder = params.getString("videos_folder");
	if (folder != lastVideosFolder) showFolder(folder, scanVideos(folder), params);
}

void SourceController::showFolder(const std::string& path, const std::vector<std::string>& videos, ParamBus& params) {
	videoFiles = videos;
	lastVideosFolder = path;
	params.setString("videos_folder", path);
	params.setInt("videos_count", (int)videoFiles.size());
}

bool SourceController::applyRequest(ParamBus& params) {
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
	if (req.dub) beginCommit(req.path);
	dubOnSwap.clear();
	return true;
}

// Makes a Video the page opened ahead the live Source, with no load.
bool SourceController::installReady(const std::string& path, float loopStart, float loopLength, ParamBus& params) {
	std::vector<unsigned char> frame;
	int width = 0;
	int height = 0;
	std::unique_ptr<VideoDecoder> ready = pages.take(path, loopStart, loopLength, frame, width, height);
	if (!ready) return false;
	load.cancel();
	cancelCommit();
	if (webcamActive) {
		webcam->stop();
		webcamActive = false;
	}
	if (fileActive) pages.retire(std::move(fileSrc));
	fileSrc = std::move(ready);
	fileActive = true;
	seenKind = (int)SourceKind::File;
	params.setInt("source_kind", (int)SourceKind::File);
	params.setString("file_path", path);
	presentFrame(frame, width, height);
	return true;
}

void SourceController::beginCommit(const std::string& path) {
	commitOn = true;
	commitPath = path;
}

void SourceController::cancelCommit() {
	if (!commitOn) return;
	commitOn = false;
	commitPath.clear();
}

void SourceController::presentFrame(const std::vector<unsigned char>& bgra, int width, int height) {
	if (width <= 0 || height <= 0 || bgra.empty()) return;
	picture.uploadPicture(bgra, width, height);
	frameW = width;
	frameH = height;
}
