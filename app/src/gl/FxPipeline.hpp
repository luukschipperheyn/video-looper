#pragma once

#include <OpenGL/gl3.h>
#include <string>

#include "gl/Shader.hpp"
#include "gl/FullscreenQuad.hpp"
#include "util/HotReload.hpp"

struct FxReload {
	bool vertex = false;
	bool feedback = false;
	bool diff = false;
	bool chroma = false;
	bool kaleido = false;
	bool blurX = false;
	bool blurY = false;
	bool repeat = false;
};

// p1..p3 mean what the mode says: mix, decay, zoom for Feedback; distance and angle for Repeat and Grid, which
// take their number of copies from count instead of p3.
struct FxSettings {
	int mode = 0;
	float p1 = 0;
	float p2 = 0;
	float p3 = 0;
	int count = 0;
	int repeatBlend = 0;
};

struct WholeStamp {
	GLuint tex = 0;
	float centerX = 0;
	float centerY = 0;
	float halfX = 0;
	float halfY = 0;
	float rot = 0;
};

class FxPipeline {
public:
	FxPipeline();
	~FxPipeline();
	FxPipeline(const FxPipeline&) = delete;
	FxPipeline& operator=(const FxPipeline&) = delete;

	// Loads its shaders from shaderDir.
	void init(int width, int height, const std::string& shaderDir);

	void resize(int width, int height);
	void registerWatches(HotReload& hot);
	FxReload hotReload(const std::unordered_set<std::string>& changed);
	void applyReload(const FxReload& reload);

	// Renders one FX frame at the pipeline's size from two neighbouring frames mixed by baseMix. Returns the
	// texture holding the result.
	GLuint process(GLuint tex0, GLuint tex1, float baseMix, const FxSettings& fx, const WholeStamp& stamp = {});

	// Texture containing the most recently presented FX output
	GLuint getPrevOutputTexture() const;

private:
	void destroy();
	void allocateTargets(int width, int height);
	void setWrapRepeat();

	int w;
	int h;
	GLuint postTex[2];
	GLuint postFbo[2];
	int postFront;

	ShaderProgram shaderFeedback;
	ShaderProgram shaderFeedbackDiff;
	ShaderProgram shaderFeedbackChroma;
	ShaderProgram shaderFeedbackKaleido;
	ShaderProgram shaderBlurX;
	ShaderProgram shaderBlurY;
	ShaderProgram shaderRepeat;

	FullscreenQuad quad;

	std::string vsPath;
	std::string feedbackFragPath;
	std::string feedbackDiffFragPath;
	std::string feedbackChromaFragPath;
	std::string feedbackKaleidoFragPath;
	std::string blurXFragPath;
	std::string blurYFragPath;
	std::string repeatFragPath;
};
