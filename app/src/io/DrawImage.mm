#include "io/DrawImage.hpp"

#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>

namespace {

unsigned char straight(unsigned char channel, unsigned char alpha) {
	if (alpha == 0) return 0;
	if (alpha == 255) return channel;
	return static_cast<unsigned char>(std::min(255u, (unsigned(channel) * 255u + alpha / 2u) / alpha));
}

} // namespace

bool decodeDrawImage(const std::string& png, std::vector<unsigned char>& bgra, int& width, int& height) {
	width = 0;
	height = 0;
	bgra.clear();
	if (png.size() < 8 || png.size() > 4u * 1024u * 1024u) return false;

	CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(png.data()),
	                              static_cast<CFIndex>(png.size()));
	if (!data) return false;
	CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
	CFRelease(data);
	if (!source) return false;
	CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
	CFRelease(source);
	if (!image) return false;

	size_t w = CGImageGetWidth(image);
	size_t h = CGImageGetHeight(image);
	if (w == 0 || h == 0 || w > 2048 || h > 2048) {
		CGImageRelease(image);
		return false;
	}

	// Premultiplied so ImageIO's compositor has a defined alpha. Unpremultiply below; the blend uses straight alpha.
	std::vector<unsigned char> rgba(w * h * 4);
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	CGBitmapInfo info = static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big;
	CGContextRef ctx = CGBitmapContextCreate(rgba.data(), w, h, 8, w * 4, space, info);
	CGColorSpaceRelease(space);
	if (!ctx) {
		CGImageRelease(image);
		return false;
	}
	CGContextSetBlendMode(ctx, kCGBlendModeCopy);
	CGContextDrawImage(ctx, CGRectMake(0, 0, static_cast<CGFloat>(w), static_cast<CGFloat>(h)), image);
	CGContextRelease(ctx);
	CGImageRelease(image);

	// The bitmap context's first row is the top of the image. Frames in this app are bottom row first.
	bgra.resize(w * h * 4);
	for (size_t y = 0; y < h; ++y) {
		size_t srcY = h - 1 - y;
		for (size_t x = 0; x < w; ++x) {
			size_t s = (srcY * w + x) * 4;
			size_t d = (y * w + x) * 4;
			unsigned char alpha = rgba[s + 3];
			bgra[d] = straight(rgba[s + 2], alpha);
			bgra[d + 1] = straight(rgba[s + 1], alpha);
			bgra[d + 2] = straight(rgba[s], alpha);
			bgra[d + 3] = alpha;
		}
	}
	width = static_cast<int>(w);
	height = static_cast<int>(h);
	return true;
}
