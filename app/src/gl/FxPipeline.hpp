#pragma once

#include <OpenGL/gl3.h>
#include <string>

#include "gl/Shader.hpp"
#include "gl/FullscreenQuad.hpp"
#include "util/HotReload.hpp"

class FxPipeline {
public:
	FxPipeline();
	~FxPipeline();
	FxPipeline(const FxPipeline&) = delete;
	FxPipeline& operator=(const FxPipeline&) = delete;

	void init(int width, int height,
		const std::string& vertexPath,
		const std::string& feedbackFragPath,
		const std::string& feedbackDiffFragPath,
		const std::string& feedbackChromaFragPath,
		const std::string& feedbackKaleidoFragPath,
		const std::string& blurXFragPath,
		const std::string& blurYFragPath);

	void resize(int width, int height);
	void registerWatches(HotReload& hot);
	void hotReload(HotReload& hot);

	// Render one FX frame. Returns the texture containing the result for this frame.
	GLuint process(int viewportW, int viewportH,
		GLuint ringTex0, GLuint ringTex1, float baseMix,
		int fxMode, float p1, float p2, float p3,
		FullscreenQuad& quad);

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

	std::string vsPath;
	std::string feedbackFragPath;
	std::string feedbackDiffFragPath;
	std::string feedbackChromaFragPath;
	std::string feedbackKaleidoFragPath;
	std::string blurXFragPath;
	std::string blurYFragPath;
};


