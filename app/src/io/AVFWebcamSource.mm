#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>

#include <mutex>
#include <vector>
#include <cstring>

#include "io/AVFWebcamSource.hpp"

struct AVFWebcamSource::Impl {
	AVCaptureSession* session = nil;
	AVCaptureVideoDataOutput* output = nil;
	dispatch_queue_t queue = nil;
	id delegateStrong = nil;
	std::mutex frameMutex;
	std::vector<unsigned char> frameData; // BGRA
	int width = 0;
	int height = 0;
	bool newFrame = false;
};

@interface VideoDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, assign) AVFWebcamSource::Impl* impl;
@end

@implementation VideoDelegate
- (void)captureOutput:(AVCaptureOutput*)output
	didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
		   fromConnection:(AVCaptureConnection*)connection {
	(void)output;
	(void)connection;
	CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
	if (!imageBuffer) return;
	CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
	void* base = CVPixelBufferGetBaseAddress(imageBuffer);
	size_t w = CVPixelBufferGetWidth(imageBuffer);
	size_t h = CVPixelBufferGetHeight(imageBuffer);
	size_t stride = CVPixelBufferGetBytesPerRow(imageBuffer);
	if (base && w > 0 && h > 0) {
		AVFWebcamSource::Impl* impl = self.impl;
		std::lock_guard<std::mutex> lock(impl->frameMutex);
		impl->width = (int)w;
		impl->height = (int)h;
		impl->frameData.resize(h * w * 4);
		// Copy row by row, input is BGRA32
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
}
@end

AVFWebcamSource::AVFWebcamSource() {
	impl = new Impl();
}
AVFWebcamSource::~AVFWebcamSource() {
	stop();
	delete impl;
}

bool AVFWebcamSource::start(int deviceIndex) {
	printf("Starting webcam %d\n", deviceIndex);
	if (impl->session) return true;
	// Discover available cameras (prefer discovery session; add Continuity Camera type when available)
	NSMutableArray<AVCaptureDeviceType>* types =
		[NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
	if (@available(macOS 13.0, *)) {
		[types addObject:AVCaptureDeviceTypeContinuityCamera];
	}
	AVCaptureDeviceDiscoverySession* discovery =
		[AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types
															   mediaType:AVMediaTypeVideo
																position:AVCaptureDevicePositionUnspecified];
	NSArray<AVCaptureDevice*>* devices = [discovery devices];
	if (!devices || devices.count == 0) return false;
	int sel = deviceIndex;
	if (sel < 0 || sel >= (int)devices.count) sel = 0;
	AVCaptureDevice* device = devices[sel];
	NSError* error = nil;
	AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
	if (error || !input) return false;
	impl->session = [[AVCaptureSession alloc] init];
	[impl->session beginConfiguration];
	if ([impl->session canAddInput:input]) [impl->session addInput:input];
	impl->output = [[AVCaptureVideoDataOutput alloc] init];
	NSDictionary<NSString*, id>* settings =
		@{(NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
	[impl->output setVideoSettings:settings];
	[impl->output setAlwaysDiscardsLateVideoFrames:YES];
	VideoDelegate* delegate = [[VideoDelegate alloc] init];
	delegate.impl = impl;
	impl->queue = dispatch_queue_create("video_looper.avf.queue", DISPATCH_QUEUE_SERIAL);
	[impl->output setSampleBufferDelegate:delegate queue:impl->queue];
	impl->delegateStrong = delegate;
	if ([impl->session canAddOutput:impl->output]) [impl->session addOutput:impl->output];
	[impl->session commitConfiguration];
	[impl->session startRunning];
	return true;
}

void AVFWebcamSource::stop() {
	if (impl->session) {
		[impl->session stopRunning];
		impl->session = nil;
	}
	impl->output = nil;
	if (impl->queue) {
		impl->queue = nil;
	}
	if (impl->delegateStrong) {
		impl->delegateStrong = nil;
	}
	{
		std::lock_guard<std::mutex> lock(impl->frameMutex);
		impl->frameData.clear();
		impl->width = impl->height = 0;
		impl->newFrame = false;
	}
}

bool AVFWebcamSource::isOpen() const {
	return impl->session != nil;
}

bool AVFWebcamSource::grab(GLuint destTex, int& outW, int& outH) {
	std::lock_guard<std::mutex> lock(impl->frameMutex);
	if (!impl->newFrame || impl->frameData.empty() || impl->width == 0 || impl->height == 0) return false;
	glBindTexture(GL_TEXTURE_2D, destTex);
	// Upload BGRA data into existing texture, resize if needed by caller
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, impl->width, impl->height, 0, GL_BGRA, GL_UNSIGNED_BYTE,
	             impl->frameData.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	outW = impl->width;
	outH = impl->height;
	impl->newFrame = false;
	return true;
}

bool AVFWebcamSource::copyFrame(std::vector<unsigned char>& bgra, int& outW, int& outH) {
	std::lock_guard<std::mutex> lock(impl->frameMutex);
	if (!impl->newFrame || impl->frameData.empty() || impl->width == 0 || impl->height == 0) return false;
	bgra = impl->frameData;
	outW = impl->width;
	outH = impl->height;
	impl->newFrame = false;
	return true;
}
