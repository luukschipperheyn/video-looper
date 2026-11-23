#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

#include <vector>
#include <mutex>
#include <cstring>
#include <algorithm>
#include <cmath>

#include "io/AVFFileSource.hpp"

struct AVFFileSource::Impl {
	AVAssetReader* reader = nil;
	AVAssetReaderTrackOutput* output = nil;
	AVAsset* asset = nil;
	CMTime duration = kCMTimeZero;
	bool paused = false;
	bool loop = true;
	std::vector<unsigned char> frameData; // BGRA
	int width = 0;
	int height = 0;
	bool newFrame = false;
	float fps = 0.0f;
	// Playback timing state (nominal-fps gating)
	double timeAccumulatorSeconds = 0.0;
	double framePeriodSeconds = 0.0;

	// Loop range state (normalized 0..1)
	float loopStartPct = 0.0f;
	float loopEndPct = 1.0f;
	bool ended = false;
};

// (Re)create a reader at the current loop range (private member)
bool AVFFileSource::recreateReaderAtRangeStart() {
	Impl* impl = this->impl;
	if (!impl || !impl->asset) return false;
	// Clamp and compute time range
	float s = std::max(0.0f, std::min(impl->loopStartPct, 1.0f));
	float e = std::max(0.0f, std::min(impl->loopEndPct, 1.0f));
	if (e <= s + 0.0005f) e = std::min(1.0f, s + 0.0005f); // minimal duration

	NSError* error = nil;
	if (impl->reader) {
		[impl->reader cancelReading];
		impl->reader = nil;
	}
	impl->output = nil;

	impl->reader = [[AVAssetReader alloc] initWithAsset:impl->asset error:&error];
	if (error || !impl->reader) return false;

	NSArray<AVAssetTrack*>* tracks = [impl->asset tracksWithMediaType:AVMediaTypeVideo];
	if (tracks.count == 0) return false;
	AVAssetTrack* track = tracks.firstObject;

	NSDictionary* settings = @{(NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
	AVAssetReaderTrackOutput* out = [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:settings];
	if ([impl->reader canAddOutput:out]) [impl->reader addOutput:out];

	// Apply time range on the reader
	CMTimeRange tr;
	tr.start = CMTimeMultiplyByFloat64(impl->duration, s);
	CMTime end = CMTimeMultiplyByFloat64(impl->duration, e);
	tr.duration = CMTimeSubtract(end, tr.start);
	impl->reader.timeRange = tr;

	impl->output = out;
	[impl->reader startReading];
	impl->timeAccumulatorSeconds = 0.0; // reset timing on restart
	impl->ended = false;
	return true;
}

AVFFileSource::AVFFileSource() {
	impl = new Impl();
}
AVFFileSource::~AVFFileSource() {
	stop();
	delete impl;
}

bool AVFFileSource::startFile(const std::string& path) {
	stop();
	NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
	impl->asset = [AVAsset assetWithURL:url];
	if (!impl->asset) return false;
	NSError* error = nil;
	impl->reader = [[AVAssetReader alloc] initWithAsset:impl->asset error:&error];
	if (error || !impl->reader) return false;
	NSArray<AVAssetTrack*>* tracks = [impl->asset tracksWithMediaType:AVMediaTypeVideo];
	if (tracks.count == 0) return false;
	AVAssetTrack* track = tracks.firstObject;
	// Estimate nominal fps
	if (track.nominalFrameRate > 0.0f)
		impl->fps = track.nominalFrameRate;
	else
		impl->fps = 0.0f;
	NSDictionary* settings = @{(NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
	impl->output = [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:settings];
	if ([impl->reader canAddOutput:impl->output]) [impl->reader addOutput:impl->output];
	impl->duration = impl->asset.duration;
	impl->paused = false;
	impl->loop = true;
	impl->timeAccumulatorSeconds = 0.0;
	impl->framePeriodSeconds = (impl->fps > 0.0f) ? (1.0 / static_cast<double>(impl->fps)) : 0.0;

	// Initialize loop range defaults and recreate reader so timeRange is applied
	impl->loopStartPct = 0.0f;
	impl->loopEndPct = 1.0f;
	recreateReaderAtRangeStart();
	return true;
}

void AVFFileSource::setPaused(bool p) {
	impl->paused = p;
}
void AVFFileSource::setLoop(bool l) {
	impl->loop = l;
}
void AVFFileSource::tick(double dt) {
	if (!impl->reader) return;
	if (impl->paused) return;
	if (impl->framePeriodSeconds <= 0.0) return; // unknown fps -> no gating
	impl->timeAccumulatorSeconds += (dt > 0.0 ? dt : 0.0);
}

void AVFFileSource::stop() {
	if (impl->reader) {
		[impl->reader cancelReading];
		impl->reader = nil;
	}
	impl->output = nil;
	impl->asset = nil;
	impl->frameData.clear();
	impl->width = impl->height = 0;
	impl->newFrame = false;
	impl->ended = false;
}

bool AVFFileSource::isOpen() const {
	return impl->reader != nil;
}

bool AVFFileSource::decode(int framesToAdvance) {
	CMSampleBufferRef sample = NULL;
	for (int i = 0; i < framesToAdvance; ++i) {
		CMSampleBufferRef s = [impl->output copyNextSampleBuffer];
		if (!s) {
			// End of stream or reached end of selected range
			if (impl->loop) {
				// Restart at loop start within selected range
				recreateReaderAtRangeStart();
			} else {
				impl->ended = true;
			}
			if (sample) {
				CFRelease(sample);
			}
			return false;
		}
		if (sample) {
			CFRelease(sample);
		}
		sample = s;
	}
	CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sample);
	CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
	void* base = CVPixelBufferGetBaseAddress(imageBuffer);
	size_t w = CVPixelBufferGetWidth(imageBuffer);
	size_t h = CVPixelBufferGetHeight(imageBuffer);
	size_t stride = CVPixelBufferGetBytesPerRow(imageBuffer);
	bool copied = base && w > 0 && h > 0;
	if (copied) {
		impl->width = (int)w;
		impl->height = (int)h;
		impl->frameData.resize(h * w * 4);
		unsigned char* dst = impl->frameData.data();
		unsigned char* src = (unsigned char*)base;
		for (size_t y = 0; y < h; ++y) {
			size_t srcOffset = y * stride;
			size_t dstOffset = (h - 1 - y) * w * 4;
			memcpy(dst + dstOffset, src + srcOffset, w * 4);
		}
		impl->newFrame = true;
	}
	CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
	CFRelease(sample);
	return copied;
}

bool AVFFileSource::grab(GLuint destTex, int& outW, int& outH) {
	if (!impl->reader || impl->paused) return false;

	// If we know the nominal fps, only advance when enough time has elapsed.
	int framesToAdvance = 1;
	if (impl->framePeriodSeconds > 0.0) {
		if (impl->timeAccumulatorSeconds < impl->framePeriodSeconds) {
			return false; // not time for next frame yet; keep showing previous
		}
		// Determine how many frames we should advance to catch up
		framesToAdvance = std::max(1, (int)floor(impl->timeAccumulatorSeconds / impl->framePeriodSeconds));
	}
	if (!decode(framesToAdvance)) return false;
	if (impl->framePeriodSeconds > 0.0) impl->timeAccumulatorSeconds -= framesToAdvance * impl->framePeriodSeconds;
	glBindTexture(GL_TEXTURE_2D, destTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, impl->width, impl->height, 0, GL_BGRA, GL_UNSIGNED_BYTE,
	             impl->frameData.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	outW = impl->width;
	outH = impl->height;
	return true;
}

bool AVFFileSource::copyFrame(std::vector<unsigned char>& bgra, int& outW, int& outH) {
	if (!impl->reader || !decode(1)) return false;
	bgra = impl->frameData;
	outW = impl->width;
	outH = impl->height;
	return true;
}

float AVFFileSource::nominalFps() const {
	return impl->fps;
}

void AVFFileSource::setLoopRange(float startPct, float endPct) {
	// Clamp and ensure minimal duration
	float s = std::max(0.0f, std::min(startPct, 1.0f));
	float e = std::max(0.0f, std::min(endPct, 1.0f));
	if (e <= s + 0.0005f) e = std::min(1.0f, s + 0.0005f);

	// Avoid unnecessary resets
	const float eps = 1e-4f;
	bool changed = (std::fabs(s - impl->loopStartPct) > eps) || (std::fabs(e - impl->loopEndPct) > eps);
	impl->loopStartPct = s;
	impl->loopEndPct = e;
	if (changed && impl->asset) {
		// Jump to new start immediately
		recreateReaderAtRangeStart();
	}
}

void AVFFileSource::restartAtLoopStart() {
	if (!impl || !impl->asset) return;
	recreateReaderAtRangeStart();
}

bool AVFFileSource::ended() const {
	return impl->ended;
}
