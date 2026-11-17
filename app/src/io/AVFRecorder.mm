#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include "io/AVFRecorder.hpp"
@interface _RecObj : NSObject
@end
@implementation _RecObj
@end
#include <mutex>
#include <vector>

struct AVFRecorder::Impl {
	AVAssetWriter* writer = nil;
	AVAssetWriterInput* input = nil;
	AVAssetWriterInputPixelBufferAdaptor* adaptor = nil;
	int width = 0;
	int height = 0;
	double fps = 0.0;
	int64_t frameIndex = 0;
	// Async encoding state
	bool running = false;
	dispatch_queue_t encodeQueue = nil;
	std::mutex frameMutex;
	std::vector<unsigned char> staging;
	int stagingStride = 0;
	bool hasFrame = false;
};

AVFRecorder::AVFRecorder() {
	impl = new Impl();
}
AVFRecorder::~AVFRecorder() {
	stop();
	delete impl;
}

bool AVFRecorder::start(const std::string& outputPath, int width, int height, double fps) {
	stop();
	impl->width = width;
	impl->height = height;
	impl->fps = fps > 0.0 ? fps : 60.0;
	impl->frameIndex = 0;

	NSError* error = nil;
	NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:outputPath.c_str()]];
	NSLog(@"[rec] url %@", url);
	[[NSFileManager defaultManager] removeItemAtURL:url error:nil];
	impl->writer = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeQuickTimeMovie error:&error];
	if (error || !impl->writer) {
		NSLog(@"[rec] writer init failed: %@", error);
		return false;
	}

	NSDictionary* outputSettings = @{
		AVVideoCodecKey : AVVideoCodecTypeH264,
		AVVideoWidthKey : @(width),
		AVVideoHeightKey : @(height),
		AVVideoCompressionPropertiesKey :
			@{AVVideoAverageBitRateKey : @(8 * 1000 * 1000), AVVideoProfileLevelKey : AVVideoProfileLevelH264High41}
	};

	impl->input = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeVideo outputSettings:outputSettings];
	impl->input.expectsMediaDataInRealTime = YES;
	if (!impl->input) {
		NSLog(@"[rec] input create failed");
		return false;
	}

	NSDictionary* srcAttrs = @{
		(NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
		(NSString*)kCVPixelBufferWidthKey : @(width),
		(NSString*)kCVPixelBufferHeightKey : @(height),
	};
	impl->adaptor = [[AVAssetWriterInputPixelBufferAdaptor alloc] initWithAssetWriterInput:impl->input
															   sourcePixelBufferAttributes:srcAttrs];
	if (![impl->adaptor isKindOfClass:[AVAssetWriterInputPixelBufferAdaptor class]]) {
		NSLog(@"[rec] adaptor create failed");
		return false;
	}
	if ([impl->writer canAddInput:impl->input]) {
		[impl->writer addInput:impl->input];
	} else {
		NSLog(@"[rec] cannot add input");
		return false;
	}
	if (![impl->writer startWriting]) {
		NSLog(@"[rec] startWriting failed: %@", impl->writer.error);
		return false;
	}
	[impl->writer startSessionAtSourceTime:kCMTimeZero];
	NSLog(@"[rec] writer started");
	impl->running = true;
	impl->encodeQueue = dispatch_queue_create("video_looper.rec.encode", DISPATCH_QUEUE_SERIAL);
	AVAssetWriterInput* inputRef = impl->input;
	AVAssetWriterInputPixelBufferAdaptor* adaptorRef = impl->adaptor;
	Impl* strongImpl = impl;
	[impl->input
		requestMediaDataWhenReadyOnQueue:impl->encodeQueue
							  usingBlock:^{
								while (strongImpl->running && [inputRef isReadyForMoreMediaData]) {
									// Grab latest frame
									std::vector<unsigned char> local;
									int localStride = 0;
									{
										std::lock_guard<std::mutex> lock(strongImpl->frameMutex);
										if (!strongImpl->hasFrame) {
											break;
										}
										local = strongImpl->staging; // copy
										localStride = strongImpl->stagingStride;
										strongImpl->hasFrame = false;
									}
									if (local.empty()) break;
									CVPixelBufferRef pb = nullptr;
									if (adaptorRef.pixelBufferPool) {
										CVReturn cr = CVPixelBufferPoolCreatePixelBuffer(
											kCFAllocatorDefault, adaptorRef.pixelBufferPool, &pb);
										if (cr != kCVReturnSuccess || !pb) {
											continue;
										}
									} else {
										NSDictionary* attrs = @{
											(NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
											(NSString*)kCVPixelBufferWidthKey : @(strongImpl->width),
											(NSString*)kCVPixelBufferHeightKey : @(strongImpl->height),
										};
										CVReturn r = CVPixelBufferCreate(kCFAllocatorDefault, strongImpl->width,
				                                                         strongImpl->height, kCVPixelFormatType_32BGRA,
				                                                         (__bridge CFDictionaryRef)attrs, &pb);
										if (r != kCVReturnSuccess || !pb) {
											continue;
										}
									}
									CVPixelBufferLockBaseAddress(pb, 0);
									void* dstBase = CVPixelBufferGetBaseAddress(pb);
									size_t dstStride = CVPixelBufferGetBytesPerRow(pb);
									int copyWidth = std::min<int>(localStride, (int)dstStride);
									for (int y = 0; y < strongImpl->height; ++y) {
										memcpy((unsigned char*)dstBase + y * dstStride, local.data() + y * localStride,
				                               copyWidth);
									}
									CVPixelBufferUnlockBaseAddress(pb, 0);
									CMTime pts = CMTimeMake(strongImpl->frameIndex, (int32_t)llround(strongImpl->fps));
									strongImpl->frameIndex++;
									[adaptorRef appendPixelBuffer:pb withPresentationTime:pts];
									CVPixelBufferRelease(pb);
								}
							  }];
	return true;
}

void AVFRecorder::stop() {
	@autoreleasepool {
		impl->running = false;
		if (impl->writer) {
			[impl->input markAsFinished];
			__block dispatch_semaphore_t sema = dispatch_semaphore_create(0);
			[impl->writer finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(sema); }];
			dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5.0 * NSEC_PER_SEC));
			(void)dispatch_semaphore_wait(sema, timeout);
		}
		impl->writer = nil;
		impl->input = nil;
		impl->adaptor = nil;
		// clear staging
		std::lock_guard<std::mutex> lock(impl->frameMutex);
		impl->staging.clear();
		impl->hasFrame = false;
		impl->stagingStride = 0;
	}
}

bool AVFRecorder::isRecording() const {
	return impl->writer != nil;
}

bool AVFRecorder::appendBGRAFrame(const unsigned char* data, int strideBytes) {
	if (!impl->writer || !impl->input || !impl->adaptor || !impl->running) return false;
	std::lock_guard<std::mutex> lock(impl->frameMutex);
	size_t expected = static_cast<size_t>(impl->width) * static_cast<size_t>(impl->height) * 4u;
	if (impl->staging.size() != expected) impl->staging.resize(expected);
	// Copy by rows
	for (int y = 0; y < impl->height; ++y) {
		memcpy(impl->staging.data() + y * impl->width * 4, data + y * strideBytes, impl->width * 4);
	}
	impl->stagingStride = impl->width * 4;
	impl->hasFrame = true;
	return true;
}
