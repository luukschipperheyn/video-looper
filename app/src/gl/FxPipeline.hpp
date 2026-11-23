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

class FxPipeline {
public:
	FxPipeline();
	~FxPipeline();
	FxPipeline(const FxPipeline&) = delete;
	FxPipeline& operator=(const FxPipeline&) = delete;

	void init(int width, int height, const std::string& vertexPath, const std::string& feedbackFragPath,
	          const std::string& feedbackDiffFragPath, const std::string& feedbackChromaFragPath,
	          const std::string& feedbackKaleidoFragPath, const std::string& blurXFragPath,
	          const std::string& blurYFragPath, const std::string& repeatFragPath);

	void resize(int width, int height);
	void registerWatches(HotReload& hot);
	// Reads the watches and reloads. A second pipeline should applyReload with that result:
	// changed() records the new time, so only the first call in a frame sees the edit.
	FxReload hotReload(HotReload& hot);
	void applyReload(const FxReload& reload);

	// Render one FX frame. Returns the texture containing the result for this frame.
	GLuint process(int viewportW, int viewportH, GLuint ringTex0, GLuint ringTex1, float baseMix, int fxMode, float p1,
	               float p2, float p3, int repeatBlend, FullscreenQuad& quad);

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

	std::string vsPath;
	std::string feedbackFragPath;
	std::string feedbackDiffFragPath;
	std::string feedbackChromaFragPath;
	std::string feedbackKaleidoFragPath;
	std::string blurXFragPath;
	std::string blurYFragPath;
	std::string repeatFragPath;
};
