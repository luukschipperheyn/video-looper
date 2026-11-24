#pragma once

#include <OpenGL/gl3.h>
#include <string>
#include <vector>

#include "gl/FullscreenQuad.hpp"
#include "gl/Shader.hpp"
#include "util/HotReload.hpp"

// Where a Video or webcam picture sits in the frame. center and half are in frame pixels; video_fit.frag
// reads the same numbers.
struct SourcePlacement {
	float centerX;
	float centerY;
	float halfX;
	float halfY;
	float rot;
};

// The Loop at the read position: two neighbouring frames and how far the read head is between them.
struct LoopRead {
	GLuint tex0;
	GLuint tex1;
	float mix;
};

// Draws the images the instrument is made of: the framed Source, the Loop as read, and the Audition.
// Everything is drawn at the frame size given to init or resize.
class Compositor {
public:
	Compositor() = default;
	~Compositor();
	Compositor(const Compositor&) = delete;
	Compositor& operator=(const Compositor&) = delete;

	void init(int width, int height, const std::string& shaderDir);
	void resize(int width, int height);
	void registerWatches(HotReload& hot);
	void hotReload(HotReload& hot);

	// The Source, drawn into fbo, which is frame-sized.
	void drawTestPattern(GLuint fbo, float timeSeconds);
	void drawBlack(GLuint fbo);
	// bars paints the frame outside the picture black; off leaves it transparent.
	void drawPlaced(GLuint fbo, GLuint picture, const SourcePlacement& placed, bool bars);

	// The Audition: the Loop with overlay laid over it. Returns its texture, which holds until the next call.
	GLuint audition(const LoopRead& loop, GLuint overlay, float opacity, int blendMode);
	// The latest Audition. Black before the first one.
	GLuint auditionTexture() const { return auditionTex[1]; }
	// The latest Audition as BGRA rows, top row first. The stride is width * 4.
	const std::vector<unsigned char>& readAudition();

	// Draws into a rectangle of the bound framebuffer.
	void showLoop(const LoopRead& loop, int x, int y, int width, int height);
	void show(GLuint tex, int x, int y, int width, int height);

	GLuint blackTexture() const { return blackTex; }
	// For the modules that draw with the same programs: the Loop's writes, and a second window's own quad.
	ShaderProgram& blitShader() { return shaderBlit; }
	ShaderProgram& blendShader() { return shaderBlend; }
	FullscreenQuad& quad() { return fullscreen; }

private:
	void allocate();
	void destroy();
	void blend(GLuint tex0, GLuint tex1, float mix, int mode);

	int w = 0;
	int h = 0;
	GLuint auditionTex[2] = {0, 0};
	GLuint auditionFbo[2] = {0, 0};
	GLuint blackTex = 0;
	std::vector<unsigned char> readBuf;
	std::vector<unsigned char> flipBuf;

	FullscreenQuad fullscreen;
	ShaderProgram shaderTest;
	ShaderProgram shaderBlit;
	ShaderProgram shaderFit;
	ShaderProgram shaderBlend;
	std::string vsPath;
	std::string testPath;
	std::string blitPath;
	std::string fitPath;
	std::string blendPath;
};
