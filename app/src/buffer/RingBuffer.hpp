#pragma once

#include <OpenGL/gl3.h>
#include <vector>

class RingBuffer {
public:
	RingBuffer();
	~RingBuffer();
	RingBuffer(const RingBuffer&) = delete;
	RingBuffer& operator=(const RingBuffer&) = delete;

	void setup(int width, int height, int frames);
	void destroy();

	// Writing
	void bindWriteFboAt(int index);
	void unbindWriteFbo();
	void commitWrite();
	void swapBanks();

	// Reading
	GLuint sampleNearest(double frameIndex) const;
	GLuint textureAt(int index) const;
	GLuint textureAtBack(int index) const;

	int getReadBank() const { return frontBank; }
	int getWriteBank() const { return 1 - frontBank; }

	int size() const { return frameCount; }
	int getWidth() const { return texWidth; }
	int getHeight() const { return texHeight; }
	int getWriteIndex() const { return writeIndex; }

private:
	void createStorage();

	int texWidth;
	int texHeight;
	int frameCount;
	int writeIndex;
	int frontBank; // 0 or 1, currently active read bank for all indices
	std::vector<GLuint> textures[2];
	std::vector<GLuint> fbos[2];
};
