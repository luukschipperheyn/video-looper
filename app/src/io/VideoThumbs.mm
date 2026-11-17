#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>

#include "io/VideoThumbs.hpp"

#include <filesystem>

namespace {

// A JPEG of the Video's first frame with a long edge of 160 px, or empty on failure.
std::string thumbJpeg(const std::string& path) {
	std::string jpeg;
	std::string* out = &jpeg;
	@autoreleasepool {
		NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
		AVAssetImageGenerator* generator =
			[AVAssetImageGenerator assetImageGeneratorWithAsset:[AVURLAsset URLAssetWithURL:url options:nil]];
		generator.appliesPreferredTrackTransform = YES;
		generator.maximumSize = CGSizeMake(160, 160);
		dispatch_semaphore_t done = dispatch_semaphore_create(0);
		[generator generateCGImageAsynchronouslyForTime:kCMTimeZero
		                              completionHandler:^(CGImageRef image, CMTime, NSError*) {
			                              if (image) {
				                              NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:image];
				                              NSData* data = [rep representationUsingType:NSBitmapImageFileTypeJPEG
				                                                               properties:@{NSImageCompressionFactor: @0.7}];
				                              if (data) out->assign(static_cast<const char*>(data.bytes), data.length);
				                              [rep release];
			                              }
			                              dispatch_semaphore_signal(done);
		                              }];
		dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
		dispatch_release(done);
	}
	return jpeg;
}

} // namespace

VideoThumbs::VideoThumbs() : worker(&VideoThumbs::run, this) {}

VideoThumbs::~VideoThumbs() {
	{
		std::lock_guard<std::mutex> lock(mutex);
		quit = true;
	}
	wake.notify_one();
	worker.join();
}

void VideoThumbs::setVideos(std::vector<std::string> next) {
	{
		std::lock_guard<std::mutex> lock(mutex);
		paths = std::move(next);
		++generation;
		made.clear();
		changed = true;
	}
	wake.notify_one();
}

bool VideoThumbs::take(std::vector<std::pair<std::string, std::string>>& out) {
	std::lock_guard<std::mutex> lock(mutex);
	if (!changed) return false;
	changed = false;
	out = made;
	return true;
}

void VideoThumbs::run() {
	uint64_t seen = 0;
	std::unique_lock<std::mutex> lock(mutex);
	for (;;) {
		wake.wait(lock, [&] { return quit || generation != seen; });
		if (quit) return;
		seen = generation;
		std::vector<std::string> todo = paths;
		for (const std::string& path : todo) {
			lock.unlock();
			std::string jpeg = thumbJpeg(path);
			lock.lock();
			if (quit || generation != seen) break;
			if (jpeg.empty()) continue;
			made.emplace_back(std::filesystem::path(path).filename().string(), std::move(jpeg));
			changed = true;
		}
	}
}
