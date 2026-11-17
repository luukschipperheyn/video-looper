#include "buffer/RingBuffer.hpp"

#include <cassert>

RingBuffer::RingBuffer()
	: texWidth(0), texHeight(0), frameCount(0), writeIndex(0), frontBank(0) {}

RingBuffer::~RingBuffer() { destroy(); }

void RingBuffer::destroy() {
    for (int b = 0; b < 2; ++b) {
        if (!fbos[b].empty()) {
            glDeleteFramebuffers(static_cast<GLsizei>(fbos[b].size()), fbos[b].data());
            fbos[b].clear();
        }
        if (!textures[b].empty()) {
            glDeleteTextures(static_cast<GLsizei>(textures[b].size()), textures[b].data());
            textures[b].clear();
        }
    }
    texWidth = texHeight = frameCount = writeIndex = 0;
    frontBank = 0;
}

void RingBuffer::setup(int width, int height, int frames) {
	destroy();
	texWidth = width;
	texHeight = height;
	frameCount = frames;
	createStorage();
	writeIndex = 0;
    frontBank = 0;
}

void RingBuffer::createStorage() {
	assert(texWidth > 0 && texHeight > 0 && frameCount > 0);
    for (int b = 0; b < 2; ++b) {
		textures[b].resize(frameCount, 0);
		fbos[b].resize(frameCount, 0);
		glGenTextures(frameCount, textures[b].data());
		glGenFramebuffers(frameCount, fbos[b].data());
		for (int i = 0; i < frameCount; ++i) {
			glBindTexture(GL_TEXTURE_2D, textures[b][i]);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			glBindFramebuffer(GL_FRAMEBUFFER, fbos[b][i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[b][i], 0);
			GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			assert(status == GL_FRAMEBUFFER_COMPLETE);
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void RingBuffer::bindWriteFboAt(int index) {
	assert(frameCount > 0);
    int backBank = 1 - frontBank;
	int idx = index % frameCount;
	if (idx < 0) idx += frameCount;
	writeIndex = idx;
	glBindFramebuffer(GL_FRAMEBUFFER, fbos[backBank][idx]);
}

void RingBuffer::unbindWriteFbo() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RingBuffer::commitWrite() {
    // No-op for global A/B scheme; flipping happens via swapBanks() on wrap
}

void RingBuffer::swapBanks() {
    frontBank = 1 - frontBank;
}

GLuint RingBuffer::textureAt(int index) const {
    if (frameCount <= 0) return 0;
    int idx = index % frameCount;
    if (idx < 0) idx += frameCount;
    return textures[frontBank][idx];
}

GLuint RingBuffer::textureAtBack(int index) const {
    if (frameCount <= 0) return 0;
    int idx = index % frameCount;
    if (idx < 0) idx += frameCount;
    int backBank = 1 - frontBank;
    return textures[backBank][idx];
}

GLuint RingBuffer::sampleNearest(double frameIndex) const {
	int idx = static_cast<int>(frameIndex + 0.5);
	return textureAt(idx);
}


