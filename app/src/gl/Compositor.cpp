#include "gl/Compositor.hpp"

#include "util/GlUtils.hpp"

#include <OpenGL/OpenGL.h>
#include <cstdio>
#include <cstring>

Compositor::Compositor() {
	mainContext = CGLGetCurrentContext();
}

Compositor::~Compositor() {
	destroy();
	if (blackTex) glDeleteTextures(1, &blackTex);
	if (pictureTex) glDeleteTextures(1, &pictureTex);
	if (drawTex) glDeleteTextures(1, &drawTex);
}

void Compositor::init(int width, int height, const std::string& shaderDir) {
	w = width;
	h = height;
	vsPath = shaderDir + "/fullscreen.vert";
	testPath = shaderDir + "/test_pattern.frag";
	blitPath = shaderDir + "/texture_blit.frag";
	fitPath = shaderDir + "/video_fit.frag";
	blendPath = shaderDir + "/texture_blend_two.frag";
	shaderTest.loadFromFiles(vsPath, testPath);
	shaderBlit.loadFromFiles(vsPath, blitPath);
	shaderFit.loadFromFiles(vsPath, fitPath);
	shaderBlend.loadFromFiles(vsPath, blendPath);

	blackTex = createTexture2D(1, 1);
	const unsigned char blackPx[4] = {0, 0, 0, 255};
	glBindTexture(GL_TEXTURE_2D, blackTex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, blackPx);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenTextures(1, &pictureTex);
	glBindTexture(GL_TEXTURE_2D, pictureTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	allocate();
}

void Compositor::uploadPicture(const std::vector<unsigned char>& bgra, int width, int height) {
	glBindTexture(GL_TEXTURE_2D, pictureTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra.data());
	glBindTexture(GL_TEXTURE_2D, 0);
}

void Compositor::uploadDraw(const std::vector<unsigned char>& bgra, int width, int height) {
	if (width <= 0 || height <= 0 || bgra.empty()) return;
	if (!drawTex) {
		glGenTextures(1, &drawTex);
		glBindTexture(GL_TEXTURE_2D, drawTex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	} else {
		glBindTexture(GL_TEXTURE_2D, drawTex);
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra.data());
	glBindTexture(GL_TEXTURE_2D, 0);
}

void Compositor::resize(int width, int height) {
	if (width == w && height == h) return;
	w = width;
	h = height;
	destroy();
	allocate();
}

void Compositor::allocate() {
	sourceTex = createTexture2D(w, h);
	sourceFbo = createFbo(sourceTex);
	for (int i = 0; i < 2; ++i) {
		auditionTex[i] = createTexture2D(w, h);
		auditionFbo[i] = createFbo(auditionTex[i]);
	}
	programTex = createTexture2D(w, h);
	programFbo = createFbo(programTex);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Compositor::destroy() {
	if (sourceFbo) glDeleteFramebuffers(1, &sourceFbo);
	if (sourceTex) glDeleteTextures(1, &sourceTex);
	sourceFbo = 0;
	sourceTex = 0;
	for (int i = 0; i < 2; ++i) {
		if (auditionFbo[i]) glDeleteFramebuffers(1, &auditionFbo[i]);
		if (auditionTex[i]) glDeleteTextures(1, &auditionTex[i]);
		auditionFbo[i] = 0;
		auditionTex[i] = 0;
	}
	if (programFbo) glDeleteFramebuffers(1, &programFbo);
	if (programTex) glDeleteTextures(1, &programTex);
	programFbo = 0;
	programTex = 0;
}

void Compositor::registerWatches(HotReload& hot) {
	hot.watch(vsPath);
	hot.watch(testPath);
	hot.watch(blitPath);
	hot.watch(fitPath);
	hot.watch(blendPath);
}

void Compositor::hotReload(const std::unordered_set<std::string>& changed) {
	bool vertex = changed.count(vsPath) != 0;
	if (changed.count(testPath) || vertex) shaderTest.loadFromFiles(vsPath, testPath);
	if (changed.count(blitPath) || vertex) shaderBlit.loadFromFiles(vsPath, blitPath);
	if (changed.count(fitPath) || vertex) shaderFit.loadFromFiles(vsPath, fitPath);
	if (changed.count(blendPath) || vertex) shaderBlend.loadFromFiles(vsPath, blendPath);
}

WholeStamp Compositor::drawSource(SourceKind kind, float timeSeconds, const SourcePlacement& placed, bool bars,
                                  bool hasPicture) {
	WholeStamp stamp;
	if (kind == SourceKind::Shader)
		drawTestPattern(timeSeconds);
	else if (kind == SourceKind::Black)
		drawBlack();
	else if (kind == SourceKind::Draw)
		drawDraw();
	else if (hasPicture) {
		stamp.tex = pictureTex;
		stamp.centerX = placed.centerX;
		stamp.centerY = placed.centerY;
		stamp.halfX = placed.halfX;
		stamp.halfY = placed.halfY;
		stamp.rot = placed.rot;
		drawPlacedPicture(placed, bars);
	}
	return stamp;
}

void Compositor::drawTestPattern(float timeSeconds) {
	glBindFramebuffer(GL_FRAMEBUFFER, sourceFbo);
	glViewport(0, 0, w, h);
	shaderTest.use();
	shaderTest.setUniform("uTime", timeSeconds);
	shaderTest.setUniform("uResolution", static_cast<float>(w), static_cast<float>(h));
	fullscreen.draw();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Compositor::drawBlack() {
	glBindFramebuffer(GL_FRAMEBUFFER, sourceFbo);
	glViewport(0, 0, w, h);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Compositor::drawDraw() {
	glBindFramebuffer(GL_FRAMEBUFFER, sourceFbo);
	glViewport(0, 0, w, h);
	GLboolean wasBlend = glIsEnabled(GL_BLEND);
	glDisable(GL_BLEND);
	if (drawTex) {
		shaderBlit.use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, drawTex);
		shaderBlit.setUniform("uTex", 0);
		fullscreen.draw();
	} else {
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	if (wasBlend) glEnable(GL_BLEND);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Compositor::drawPlacedPicture(const SourcePlacement& placed, bool bars) {
	glBindFramebuffer(GL_FRAMEBUFFER, sourceFbo);
	glViewport(0, 0, w, h);
	shaderFit.use();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, pictureTex);
	shaderFit.setUniform("uTex", 0);
	shaderFit.setUniform("uFrame", static_cast<float>(w), static_cast<float>(h));
	shaderFit.setUniform("uCenter", placed.centerX, placed.centerY);
	shaderFit.setUniform("uHalf", placed.halfX, placed.halfY);
	shaderFit.setUniform("uRot", placed.rot);
	shaderFit.setUniform("uOutside", bars ? 1.0f : 0.0f);
	fullscreen.draw();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Compositor::blit(GLuint tex) {
	shaderBlit.use();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);
	shaderBlit.setUniform("uTex", 0);
	quadForCurrent().draw();
}

void Compositor::blend(GLuint tex0, GLuint tex1, float mix, int mode) {
	shaderBlend.use();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex0);
	shaderBlend.setUniform("uTex0", 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, tex1);
	shaderBlend.setUniform("uTex1", 1);
	shaderBlend.setUniform("uMix", mix);
	shaderBlend.setUniform("uMode", mode);
	fullscreen.draw();
}

GLuint Compositor::image(Picture picture) const {
	switch (picture) {
	case Picture::Source:
		return sourceTex;
	case Picture::Audition:
		return auditionTex[1];
	case Picture::Program:
		return programTex;
	}
	return 0;
}

void Compositor::audition(const LoopRead& loop, GLuint overlay, float opacity, int blendMode) {
	glBindFramebuffer(GL_FRAMEBUFFER, auditionFbo[0]);
	glViewport(0, 0, w, h);
	blend(loop.tex0, loop.tex1, loop.mix, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, auditionFbo[1]);
	glViewport(0, 0, w, h);
	blend(auditionTex[0], overlay, opacity, blendMode);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Compositor::program(const LoopRead& loop, GLuint fxOutput) {
	glBindFramebuffer(GL_FRAMEBUFFER, programFbo);
	glViewport(0, 0, w, h);
	if (fxOutput)
		blit(fxOutput);
	else
		blend(loop.tex0, loop.tex1, loop.mix, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

const std::vector<unsigned char>& Compositor::read(Picture picture) {
	GLuint fbo = 0;
	switch (picture) {
	case Picture::Source:
		fbo = sourceFbo;
		break;
	case Picture::Audition:
		fbo = auditionFbo[1];
		break;
	case Picture::Program:
		fbo = programFbo;
		break;
	}
	const size_t stride = static_cast<size_t>(w) * 4u;
	const size_t size = stride * static_cast<size_t>(h);
	if (readBuf.size() != size) readBuf.assign(size, 0);
	if (flipBuf.size() != size) flipBuf.assign(size, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, readBuf.data());
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) std::fprintf(stderr, "[rec] glReadPixels error: 0x%04x\n", err);
	for (int y = 0; y < h; ++y)
		std::memcpy(flipBuf.data() + static_cast<size_t>(h - 1 - y) * stride, readBuf.data() + y * stride, stride);
	return flipBuf;
}

void Compositor::show(GLuint tex, int x, int y, int width, int height) {
	glViewport(x, y, width, height);
	blit(tex);
}

FullscreenQuad& Compositor::quadForCurrent() {
	CGLContextObj ctx = CGLGetCurrentContext();
	if (ctx == static_cast<CGLContextObj>(mainContext)) return fullscreen;
	if (!sharedQuad) {
		sharedQuad = std::make_unique<FullscreenQuad>();
		sharedContext = ctx;
	}
	return *sharedQuad;
}

void Compositor::releaseSharedQuad() {
	if (!sharedQuad || CGLGetCurrentContext() != static_cast<CGLContextObj>(sharedContext)) return;
	sharedQuad.reset();
	sharedContext = nullptr;
}
