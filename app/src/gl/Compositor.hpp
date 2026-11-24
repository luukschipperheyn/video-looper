#pragma once

#include <OpenGL/gl3.h>
#include <string>
#include <vector>

#include "gl/FullscreenQuad.hpp"
#include "gl/FxPipeline.hpp"
#include "gl/PictureChoice.hpp"
#include "gl/PictureRead.hpp"
#include "gl/Shader.hpp"
#include "io/VideoSource.hpp"
#include "util/HotReload.hpp"

#include <memory>

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

class Compositor : public PicturePort, public PictureRead {
public:
	Compositor();
	~Compositor() override;
	Compositor(const Compositor&) = delete;
	Compositor& operator=(const Compositor&) = delete;

	void init(int width, int height, const std::string& shaderDir);
	void resize(int width, int height);
	void registerWatches(HotReload& hot);
	void hotReload(const std::unordered_set<std::string>& changed);

	// The latest Video or webcam picture at its own pixel size. SourceController uploads into it.
	unsigned int pictureTexture() override { return pictureTex; }
	void uploadPicture(const std::vector<unsigned char>& bgra, int width, int height) override;

	// Replaces the Draw picture. Straight-alpha BGRA, bottom row first, stretched over the loop frame.
	void uploadDraw(const std::vector<unsigned char>& bgra, int width, int height);
	WholeStamp drawSource(SourceKind kind, float timeSeconds, const SourcePlacement& placed, bool bars,
	                      bool hasPicture);

	GLuint image(Picture picture) const;
	void audition(const LoopRead& loop, GLuint overlay, float opacity, int blendMode);
	void program(const LoopRead& loop, GLuint fxOutput);
	// BGRA rows, top row first. The stride is width * 4.
	const std::vector<unsigned char>& read(Picture picture) override;

	// Draws into a rectangle of the bound framebuffer.
	void show(GLuint tex, int x, int y, int width, int height);
	void blit(GLuint tex);
	void blend(GLuint base, GLuint over, float opacity, int mode);
	// Drops the quad for the current shared context. Call that context current first.
	void releaseSharedQuad();

	GLuint blackTexture() const { return blackTex; }

private:
	void allocate();
	void destroy();
	void drawTestPattern(float timeSeconds);
	void drawBlack();
	void drawDraw();
	void drawPlacedPicture(const SourcePlacement& placed, bool bars);
	FullscreenQuad& quadForCurrent();

	int w = 0;
	int h = 0;
	void* mainContext = nullptr;
	void* sharedContext = nullptr;
	std::unique_ptr<FullscreenQuad> sharedQuad;
	GLuint sourceTex = 0;
	GLuint sourceFbo = 0;
	GLuint pictureTex = 0;
	GLuint drawTex = 0;
	GLuint auditionTex[2] = {0, 0};
	GLuint auditionFbo[2] = {0, 0};
	GLuint programTex = 0;
	GLuint programFbo = 0;
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

class RingBuffer;
class LooperEngine;

void initFrame(int width, int height, const std::string& shaderDir, RingBuffer& ring, LooperEngine& looper,
               Compositor& picture, FxPipeline& program, FxPipeline& source);
void resizeFrame(int width, int height, RingBuffer& ring, LooperEngine& looper, Compositor& picture,
                 FxPipeline& program, FxPipeline& source);
void reloadShaders(HotReload& hot, Compositor& picture, FxPipeline& program, FxPipeline& source);
