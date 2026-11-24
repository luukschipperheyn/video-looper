#include "gl/FxPipeline.hpp"

#include "util/GlUtils.hpp"
#include "util/Repeat.hpp"

#include <cmath>

FxPipeline::FxPipeline() : w(0), h(0), postFront(0) {
	postTex[0] = 0;
	postTex[1] = 0;
	postFbo[0] = 0;
	postFbo[1] = 0;
}

FxPipeline::~FxPipeline() {
	destroy();
}

void FxPipeline::init(int width, int height, const std::string& shaderDir) {
	w = width;
	h = height;
	vsPath = shaderDir + "/fullscreen.vert";
	feedbackFragPath = shaderDir + "/feedback.frag";
	feedbackDiffFragPath = shaderDir + "/feedback_diff.frag";
	feedbackChromaFragPath = shaderDir + "/feedback_chroma.frag";
	feedbackKaleidoFragPath = shaderDir + "/feedback_kaleido.frag";
	blurXFragPath = shaderDir + "/fx_blur_x.frag";
	blurYFragPath = shaderDir + "/fx_blur_y.frag";
	repeatFragPath = shaderDir + "/fx_repeat.frag";

	shaderFeedback.loadFromFiles(vsPath, feedbackFragPath);
	shaderFeedbackDiff.loadFromFiles(vsPath, feedbackDiffFragPath);
	shaderFeedbackChroma.loadFromFiles(vsPath, feedbackChromaFragPath);
	shaderFeedbackKaleido.loadFromFiles(vsPath, feedbackKaleidoFragPath);
	shaderBlurX.loadFromFiles(vsPath, blurXFragPath);
	shaderBlurY.loadFromFiles(vsPath, blurYFragPath);
	shaderRepeat.loadFromFiles(vsPath, repeatFragPath);

	allocateTargets(w, h);
	setWrapRepeat();
}

void FxPipeline::resize(int width, int height) {
	if (width == w && height == h) return;
	w = width;
	h = height;
	if (postTex[0]) glDeleteTextures(2, postTex);
	if (postFbo[0]) glDeleteFramebuffers(2, postFbo);
	allocateTargets(w, h);
	setWrapRepeat();
	postFront = 0;
}

void FxPipeline::registerWatches(HotReload& hot) {
	hot.watch(vsPath);
	hot.watch(feedbackFragPath);
	hot.watch(feedbackDiffFragPath);
	hot.watch(feedbackChromaFragPath);
	hot.watch(feedbackKaleidoFragPath);
	hot.watch(blurXFragPath);
	hot.watch(blurYFragPath);
	hot.watch(repeatFragPath);
}

FxReload FxPipeline::hotReload(const std::unordered_set<std::string>& changed) {
	FxReload reload;
	reload.vertex = changed.count(vsPath) != 0;
	reload.feedback = changed.count(feedbackFragPath) != 0;
	reload.diff = changed.count(feedbackDiffFragPath) != 0;
	reload.chroma = changed.count(feedbackChromaFragPath) != 0;
	reload.kaleido = changed.count(feedbackKaleidoFragPath) != 0;
	reload.blurX = changed.count(blurXFragPath) != 0;
	reload.blurY = changed.count(blurYFragPath) != 0;
	reload.repeat = changed.count(repeatFragPath) != 0;
	applyReload(reload);
	return reload;
}

void FxPipeline::applyReload(const FxReload& reload) {
	if (reload.vertex || reload.feedback) shaderFeedback.loadFromFiles(vsPath, feedbackFragPath);
	if (reload.vertex || reload.diff) shaderFeedbackDiff.loadFromFiles(vsPath, feedbackDiffFragPath);
	if (reload.vertex || reload.chroma) shaderFeedbackChroma.loadFromFiles(vsPath, feedbackChromaFragPath);
	if (reload.vertex || reload.kaleido) shaderFeedbackKaleido.loadFromFiles(vsPath, feedbackKaleidoFragPath);
	if (reload.vertex || reload.blurX) shaderBlurX.loadFromFiles(vsPath, blurXFragPath);
	if (reload.vertex || reload.blurY) shaderBlurY.loadFromFiles(vsPath, blurYFragPath);
	if (reload.vertex || reload.repeat) shaderRepeat.loadFromFiles(vsPath, repeatFragPath);
}

GLuint FxPipeline::process(GLuint ringTex0, GLuint ringTex1, float baseMix, const FxSettings& fx,
                           const WholeStamp& stamp) {
	const int viewportW = w;
	const int viewportH = h;
	const int fxMode = fx.mode;
	const float p1 = fx.p1;
	const float p2 = fx.p2;
	const float p3 = fx.p3;
	int postWrite = 1 - postFront;
	glBindFramebuffer(GL_FRAMEBUFFER, postFbo[postWrite]);
	glViewport(0, 0, viewportW, viewportH);

	if (fxStampsCopies(fxMode)) {
		float fw = static_cast<float>(w);
		float fh = static_cast<float>(h);
		bool grid = fxMode == kGridFxMode;
		RepeatStep step = repeatStep(p1, p2, fw, fh);
		RepeatStep across = grid ? repeatStep(p1, p2 + 0.25f, fw, fh) : RepeatStep{0.0f, 0.0f};
		int copies = grid ? gridCopies(fx.count) : repeatCopies(fx.count);
		shaderRepeat.use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, ringTex0);
		shaderRepeat.setUniform("uTex0", 0);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, ringTex1);
		shaderRepeat.setUniform("uTex1", 1);
		shaderRepeat.setUniform("uBaseMix", baseMix);
		shaderRepeat.setUniform("uStep", step.x, step.y);
		shaderRepeat.setUniform("uStep2", across.x, across.y);
		shaderRepeat.setUniform("uCount", static_cast<float>(copies));
		shaderRepeat.setUniform("uBlend", fx.repeatBlend);
		shaderRepeat.setUniform("uGrid", grid ? 1 : 0);
		bool whole = stamp.tex != 0;
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, whole ? stamp.tex : ringTex0);
		shaderRepeat.setUniform("uVideo", 2);
		shaderRepeat.setUniform("uWhole", whole ? 1 : 0);
		shaderRepeat.setUniform("uFrame", static_cast<float>(viewportW), static_cast<float>(viewportH));
		shaderRepeat.setUniform("uCenter", stamp.centerX, stamp.centerY);
		shaderRepeat.setUniform("uHalf", stamp.halfX, stamp.halfY);
		shaderRepeat.setUniform("uRot", stamp.rot);
		quad.draw();
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		postFront = postWrite;
		return postTex[postWrite];
	}

	ShaderProgram* fxShaderPtr;
	if (fxMode == 1)
		fxShaderPtr = &shaderFeedbackDiff;
	else if (fxMode == 2)
		fxShaderPtr = &shaderFeedbackChroma;
	else if (fxMode == 3)
		fxShaderPtr = &shaderFeedbackKaleido;
	else
		fxShaderPtr = &shaderFeedback; // default feedback
	ShaderProgram& fxShader = *fxShaderPtr;
	fxShader.use();

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, ringTex0);
	fxShader.setUniform("uTex0", 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, ringTex1);
	fxShader.setUniform("uTex1", 1);
	fxShader.setUniform("uBaseMix", baseMix);

	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, postTex[postFront]);
	fxShader.setUniform("uPrev", 2);

	if (fxMode == 1) {
		// Difference: p1 unused, p2 = scale, p3 = decay
		fxShader.setUniform("uDecay", p3);
		fxShader.setUniform("uScale", p2);
		fxShader.setUniform("uZoom", 0.0f);
		fxShader.setUniform("uRotate", 0.0f);
		fxShader.setUniform("uOffset", 0.0f, 0.0f);
	} else if (fxMode == 2) {
		// Chromatic: p1 = chroma amount, p2 = decay, p3 = zoom
		fxShader.setUniform("uDecay", p2);
		fxShader.setUniform("uChroma", p1);
		fxShader.setUniform("uZoom", p3);
		fxShader.setUniform("uRotate", 0.0f);
		fxShader.setUniform("uOffset", 0.0f, 0.0f);
	} else if (fxMode == 3) {
		// Kaleido: p1 = segments, p2 = decay, p3 = zoom
		fxShader.setUniform("uDecay", p2);
		fxShader.setUniform("uSegments", p1 * 12.0f + 1.0f);
		fxShader.setUniform("uZoom", p3);
		fxShader.setUniform("uRotate", 0.0f);
		fxShader.setUniform("uOffset", 0.0f, 0.0f);
	} else {
		// Feedback: p1 = feedback mix, p2 = decay, p3 = zoom
		fxShader.setUniform("uDecay", p2);
		fxShader.setUniform("uMix", p1);
		fxShader.setUniform("uZoom", p3);
		fxShader.setUniform("uRotate", 0.0f);
		fxShader.setUniform("uOffset", 0.0f, 0.0f);
	}

	quad.draw();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	bool isBlur = (fxMode == 4);
	if (isBlur) {
		int blurWrite = postFront; // overwrite old prev
		glBindFramebuffer(GL_FRAMEBUFFER, postFbo[blurWrite]);
		glViewport(0, 0, viewportW, viewportH);
		shaderBlurX.use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, postTex[postWrite]);
		shaderBlurX.setUniform("uInput", 0);
		shaderBlurX.setUniform("uTexel", 1.0f / (float)w, 1.0f / (float)h);
		shaderBlurX.setUniform("uRadius", p1);
		quad.draw();
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		glBindFramebuffer(GL_FRAMEBUFFER, postFbo[postWrite]);
		glViewport(0, 0, viewportW, viewportH);
		shaderBlurY.use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, postTex[blurWrite]);
		shaderBlurY.setUniform("uInput", 0);
		shaderBlurY.setUniform("uTexel", 1.0f / (float)w, 1.0f / (float)h);
		shaderBlurY.setUniform("uRadius", p1);
		quad.draw();
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	postFront = postWrite;
	return postTex[postWrite];
}

GLuint FxPipeline::getPrevOutputTexture() const {
	return postTex[postFront];
}

void FxPipeline::destroy() {
	if (postFbo[0]) {
		glDeleteFramebuffers(2, postFbo);
		postFbo[0] = 0;
		postFbo[1] = 0;
	}
	if (postTex[0]) {
		glDeleteTextures(2, postTex);
		postTex[0] = 0;
		postTex[1] = 0;
	}
}

void FxPipeline::allocateTargets(int width, int height) {
	postTex[0] = createTexture2D(width, height);
	postTex[1] = createTexture2D(width, height);
	postFbo[0] = createFbo(postTex[0]);
	postFbo[1] = createFbo(postTex[1]);
}

void FxPipeline::setWrapRepeat() {
	for (int i = 0; i < 2; ++i) {
		glBindTexture(GL_TEXTURE_2D, postTex[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
}
