#pragma once

#include <OpenGL/gl3.h>
#include <string>

enum class SourceKind { Shader = 0, File = 1, Black = 2, Webcam = 3, Draw = 4 };

class IVideoSource {
public:
	virtual ~IVideoSource() = default;
	virtual bool start(int deviceIndex) {
		(void)deviceIndex;
		return false;
	}
	virtual bool startFile(const std::string& path) {
		(void)path;
		return false;
	}
	virtual void tick(double) {}
	virtual void setPaused(bool) {}
	virtual void setLoop(bool) {}
	virtual void stop() = 0;
	virtual bool isOpen() const = 0;
	virtual bool grab(GLuint destTex, int& outW, int& outH) = 0; // uploads into destTex, returns dimensions
};
