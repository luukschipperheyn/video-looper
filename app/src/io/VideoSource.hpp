#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

enum class SourceKind { Shader = 0, File = 1, Black = 2, Webcam = 3, Draw = 4 };

// One open Video. AVFFileSource is the real one. texture is a GL texture name; a decoder that draws nothing
// may ignore it.
class VideoDecoder {
public:
	virtual ~VideoDecoder() = default;
	virtual bool startFile(const std::string& path) = 0;
	virtual void setPaused(bool paused) = 0;
	virtual void setLoop(bool loop) = 0;
	// Fractions of the Video's length, start < end. Applies immediately.
	virtual void setLoopRange(float start, float end) = 0;
	virtual void restartAtLoopStart() = 0;
	virtual void tick(double dtSeconds) = 0;
	// Uploads a new frame into texture and reports its size. False when there is no new frame.
	virtual bool grab(unsigned int texture, int& outW, int& outH) = 0;
	// Decodes the next frame into bgra (BGRA, bottom row first) without touching GL.
	virtual bool copyFrame(std::vector<unsigned char>& bgra, int& outW, int& outH) = 0;
	// True after a non-looping read passes the end of the range.
	virtual bool ended() const = 0;
	virtual float nominalFps() const = 0;
	virtual void stop() = 0;
};

// One open webcam. AVFWebcamSource is the real one.
class CameraDecoder {
public:
	virtual ~CameraDecoder() = default;
	virtual bool start(int deviceIndex) = 0;
	virtual bool grab(unsigned int texture, int& outW, int& outH) = 0;
	// Copies the newest unseen frame into bgra (BGRA, bottom row first) without touching GL.
	virtual bool copyFrame(std::vector<unsigned char>& bgra, int& outW, int& outH) = 0;
	virtual void stop() = 0;
};

// Makes closed decoders. Called from the main thread and from loader threads.
struct DecoderFactory {
	std::function<std::unique_ptr<VideoDecoder>()> video;
	std::function<std::unique_ptr<CameraDecoder>()> camera;
};

// Where a Video or webcam picture goes at its own pixel size, before it is placed in the frame. The Compositor
// is the real one.
class PicturePort {
public:
	virtual ~PicturePort() = default;
	virtual unsigned int pictureTexture() = 0;
	virtual void uploadPicture(const std::vector<unsigned char>& bgra, int width, int height) = 0;
};
