#pragma once

#include <OpenGL/gl3.h>
#include <string>
#include <vector>

#include "gl/FullscreenQuad.hpp"
#include "gl/Shader.hpp"
#include "io/VideoSource.hpp"
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

// Draws the images the instrument is made of: the framed Source, the Loop as read, and the Audition, and owns
// their targets. Everything is drawn at the frame size given to init or resize.
class Compositor : public PicturePort {
public:
	Compositor() = default;
	~Compositor() override;
	Compositor(const Compositor&) = delete;
	Compositor& operator=(const Compositor&) = delete;

	void init(int width, int height, const std::string& shaderDir);
	void resize(int width, int height);
	void registerWatches(HotReload& hot);
	void hotReload(HotReload& hot);

	// The latest Video or webcam picture at its own pixel size. SourceController uploads into it.
	unsigned int pictureTexture() override { return pictureTex; }
	void uploadPicture(const std::vector<unsigned char>& bgra, int width, int height) override;

	// Draws the framed Source: a test pattern, black, the Draw picture stretched over the frame, or the
	// picture placed in the frame.
	void drawTestPattern(float timeSeconds);
	void drawBlack();
	// Replaces the Draw picture. Straight-alpha BGRA, bottom row first, stretched over the loop frame.
	void uploadDraw(const std::vector<unsigned char>& bgra, int width, int height);
	// Stretches the Draw picture over the frame. Transparent until the first picture.
	void drawDraw();
	// bars paints the frame outside the picture black; off leaves it transparent.
	void drawPlacedPicture(const SourcePlacement& placed, bool bars);
	// The framed Source as last drawn.
	GLuint sourceTexture() const { return sourceTex; }

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
	GLuint sourceTex = 0;
	GLuint sourceFbo = 0;
	GLuint pictureTex = 0;
	GLuint drawTex = 0;
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
