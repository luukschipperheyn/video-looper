#include "io/SourceController.hpp"

#include "io/SourceLoad.hpp"

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

bool SourceController::tick(double dtSeconds) {
	if (takeDropDub()) dubOnSwap.clear();
	bool presented = applyRequest();
	swappedLoad = presented || swapInLoaded();
	if (!presented && swappedLoad && !dubOnSwap.empty()) {
		if (reportedKind == SourceKind::File && committedPath == dubOnSwap) beginCommit(dubOnSwap);
		dubOnSwap.clear();
	}
	bool newPicture = swappedLoad;

	bool looping = !commitOn && playLoop;
	bool paused = !commitOn && playPaused;
	if (reportedKind == SourceKind::Webcam) {
		int sw = 0, sh = 0;
		if (webcam->grab(picture.pictureTexture(), sw, sh)) {
			frameW = sw;
			frameH = sh;
			newPicture = true;
		}
	} else if (reportedKind == SourceKind::File) {
		fileSrc->setLoop(looping);
		fileSrc->setPaused(paused);
		LoopRange range = loopRange(playStart, playLength);
		fileSrc->setLoopRange(range.start, range.end);
		fileSrc->tick(dtSeconds);
		int sw = 0, sh = 0;
		if (fileSrc->grab(picture.pictureTexture(), sw, sh)) {
			frameW = sw;
			frameH = sh;
			newPicture = true;
		}
	}

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

void SourceController::loadWebcam(int index) {
	pendingVideo = {};
	startLoad(requestWebcamLoad(index));
}

void SourceController::choose(SourceKind kind) {
	cancelLoad();
	pendingVideo = {};
	dubOnSwap.clear();
	if (webcamActive) {
		webcam->stop();
		webcamActive = false;
	}
	if (fileActive) {
		fileSrc->stop();
		fileActive = false;
	}
	cancelCommit();
	reportedKind = kind;
	swappedLoad = false;
}

void SourceController::setPlayback(bool loop, bool paused, float loopStart, float loopLength) {
	playLoop = loop;
	playPaused = paused;
	playStart = loopStart;
	playLength = loopLength;
}

SourceStatus SourceController::status() const {
	return {reportedKind, committedPath, committedWebcamIndex, swappedLoad};
}

std::string SourceController::takeLoadFailure() {
	return takeFailure();
}

void SourceController::startLoad(const LoadRequest& request) {
	{
		std::lock_guard<std::mutex> lock(jobMutex);
		job = request;
	}
	jobReady.notify_one();
}

bool SourceController::swapInLoaded() {
	bool isWebcam = false;
	std::string path;
	int index = 0;
	if (!pollSwap(isWebcam, path, index)) return false;
	std::swap(webcam, spareWebcam);
	std::swap(fileSrc, spareFile);
	webcamActive = isWebcam;
	fileActive = !isWebcam;
	reportedKind = isWebcam ? SourceKind::Webcam : SourceKind::File;
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
		if (!reason.empty() || loadInflight() != request.id) {
			spareWebcam->stop();
			spareFile->stop();
		}
		resolveLoad(request.id, reason.empty(), reason);
	}
}

std::string SourceController::openSpare(const LoadRequest& request) {
	if (request.what == LoadRequest::What::Video) {
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
		if (quit || loadInflight() != request.id) return "cancelled";
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return "no frame from webcam";
}

bool SourceController::applyRequest() {
	if (!pendingVideo.on) return false;
	PendingVideo req = pendingVideo;
	pendingVideo.on = false;
	if (req.dub)
		dubOnSwap = req.path;
	else
		dubOnSwap.clear();
	if (!installReady(req.path, req.loopStart, req.loopLength)) {
		startLoad(requestVideoLoad(req.path, req.loopStart, req.loopLength));
		return false;
	}
	if (req.dub) beginCommit(req.path);
	dubOnSwap.clear();
	return true;
}

bool SourceController::installReady(const std::string& path, float loopStart, float loopLength) {
	std::vector<unsigned char> frame;
	int width = 0;
	int height = 0;
	std::unique_ptr<VideoDecoder> ready = pages.take(path, loopStart, loopLength, frame, width, height);
	if (!ready) return false;
	cancelLoad();
	cancelCommit();
	if (webcamActive) {
		webcam->stop();
		webcamActive = false;
	}
	if (fileActive) pages.retire(std::move(fileSrc));
	fileSrc = std::move(ready);
	fileActive = true;
	reportedKind = SourceKind::File;
	committedPath = path;
	committedWebcam = false;
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

SourceController::LoadRequest SourceController::requestVideoLoad(std::string path, float loopStart, float loopLength) {
	return requestLoad({0, LoadRequest::What::Video, std::move(path), 0, loopStart, loopLength});
}

SourceController::LoadRequest SourceController::requestWebcamLoad(int index) {
	return requestLoad({0, LoadRequest::What::Webcam, {}, index});
}

SourceController::LoadRequest SourceController::requestLoad(LoadRequest next) {
	std::lock_guard<std::mutex> lock(loadMutex);
	next.id = ++loadId;
	loadPending = next;
	loadPhase = LoadPhase::Loading;
	loadReason.clear();
	return next;
}

void SourceController::cancelLoad() {
	std::lock_guard<std::mutex> lock(loadMutex);
	++loadId;
	loadPhase = LoadPhase::Idle;
	loadPending = {};
	loadReason.clear();
}

uint64_t SourceController::loadInflight() const {
	std::lock_guard<std::mutex> lock(loadMutex);
	return loadPhase == LoadPhase::Idle ? 0 : loadPending.id;
}

void SourceController::resolveLoad(uint64_t id, bool ok, std::string why) {
	std::lock_guard<std::mutex> lock(loadMutex);
	if (loadPhase != LoadPhase::Loading || id != loadPending.id) return;
	loadPhase = ok ? LoadPhase::Ready : LoadPhase::Failed;
	if (!ok) dropDub = true;
	loadReason = std::move(why);
}

bool SourceController::pollSwap(bool& outWebcam, std::string& outPath, int& outWebcamIndex) {
	std::lock_guard<std::mutex> lock(loadMutex);
	if (loadPhase != LoadPhase::Ready) return false;
	loadPhase = LoadPhase::Idle;
	committedWebcam = loadPending.what == LoadRequest::What::Webcam;
	if (committedWebcam)
		committedWebcamIndex = loadPending.webcamIndex;
	else
		committedPath = loadPending.path;
	outWebcam = committedWebcam;
	outPath = committedPath;
	outWebcamIndex = committedWebcamIndex;
	return true;
}

std::string SourceController::takeFailure() {
	std::lock_guard<std::mutex> lock(loadMutex);
	if (loadPhase != LoadPhase::Failed) return {};
	loadPhase = LoadPhase::Idle;
	return std::exchange(loadReason, {});
}

bool SourceController::takeDropDub() {
	std::lock_guard<std::mutex> lock(loadMutex);
	if (!dropDub) return false;
	dropDub = false;
	return true;
}
