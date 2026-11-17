#include "looper/LooperEngine.hpp"

#include <cmath>
#include "imgui.h"

LooperEngine::LooperEngine(RingBuffer& ringBuffer)
	: ring(ringBuffer), fbWidth(0), fbHeight(0), readFrame(0.0f), lastWriteIdx(-1) {
}

void LooperEngine::init(int width, int height) {
	fbWidth = width;
	fbHeight = height;
	backWritten.assign(ring.size(), 0);
}

void LooperEngine::resize(int width, int height) {
	fbWidth = width;
	fbHeight = height;
	backWritten.assign(ring.size(), 0);
	lastWriteIdx = -1;
}

ReadState LooperEngine::advanceRead(float dtSeconds, float readFps) {
	int n = ring.size();
	float advance = readFps * dtSeconds;
	float newRead = readFrame + advance;
	int wraps = 0;
	if (n > 0 && newRead >= static_cast<float>(n)) {
		wraps = static_cast<int>(std::floor(newRead / static_cast<float>(n)));
		newRead = std::fmod(newRead, static_cast<float>(n));
		lastWriteIdx = -1;
	}
	float wrappedFrame = readFrame;
	if (wrappedFrame < 0.0f && n > 0) wrappedFrame += n;
	int idx0 = 0, idx1 = 0;
	float mixf = 0.0f;
	if (n > 0) {
		idx0 = static_cast<int>(std::floor(wrappedFrame)) % n;
		idx1 = (idx0 + 1) % n;
		mixf = wrappedFrame - std::floor(wrappedFrame);
	}
	readFrame = newRead;
	return { idx0, idx1, mixf, wraps };
}

void LooperEngine::presentNoFx(int viewportW, int viewportH,
	ShaderProgram& shaderBlendTwo,
	FullscreenQuad& quad,
	int idx0, int idx1, float mix, float gainMix) {
	glViewport(0, 0, viewportW, viewportH);
	shaderBlendTwo.use();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, ring.textureAt(idx0));
	shaderBlendTwo.setUniform("uTex0", 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, ring.textureAt(idx1));
	shaderBlendTwo.setUniform("uTex1", 1);
	shaderBlendTwo.setUniform("uMix", mix * gainMix);
	shaderBlendTwo.setUniform("uMode", 0);
	quad.draw();
}

void LooperEngine::writeAndSwap(int fbw, int fbh,
	ShaderProgram& shaderBlit,
	ShaderProgram& shaderBlendTwo,
	FullscreenQuad& quad,
	int idx0,
	int wraps,
	bool recordingActive,
	float writeOpacity,
	int recordBlendMode,
	bool fxCommit,
	unsigned int fxPrevTexture,
	unsigned int previewTexture) {
	int n = ring.size();
	if (n <= 0) return;

	int steps;
	int startIdx;
	if (lastWriteIdx < 0) {
		startIdx = idx0;
		steps = 1;
	} else {
		startIdx = (lastWriteIdx + 1) % n;
		int diff = (idx0 - lastWriteIdx + n) % n;
		if (diff == 0) {
			steps = (wraps > 0) ? n : 0;
		} else {
			steps = diff;
		}
	}

	for (int s = 0; s < steps; ++s) {
		int wi = (startIdx + s) % n;

		bool doCopy = !recordingActive && !backWritten[wi];
		bool doWrite = recordingActive || doCopy;
		if (!doWrite) continue;

		ring.bindWriteFboAt(wi);
		glViewport(0, 0, fbw, fbh);

		if (fxCommit) {
			shaderBlit.use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, fxPrevTexture);
			shaderBlit.setUniform("uTex", 0);
			quad.draw();
		} else if (recordingActive) {
			shaderBlendTwo.use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, ring.textureAt(wi));
			shaderBlendTwo.setUniform("uTex0", 0);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, previewTexture);
			shaderBlendTwo.setUniform("uTex1", 1);
			shaderBlendTwo.setUniform("uMix", writeOpacity);
			shaderBlendTwo.setUniform("uMode", recordBlendMode);
			quad.draw();
		} else {
			shaderBlit.use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, ring.textureAt(wi));
			shaderBlit.setUniform("uTex", 0);
			quad.draw();
		}
		ring.unbindWriteFbo();

		backWritten[wi] = 1;
	}

	lastWriteIdx = idx0;

	for (int i = 0; i < wraps; ++i) {
		ring.swapBanks();
		backWritten.assign(n, 0);
	}
}

void LooperEngine::drawHeadsOverlay(int viewportW, int viewportH) const {
	int nFrames = ring.size();
	if (nFrames <= 0) return;
	float rf = std::fmod(readFrame, static_cast<float>(nFrames));
	if (rf < 0.0f) rf += nFrames;
	float readNorm = rf / static_cast<float>(nFrames);
	float writeNorm = static_cast<float>(ring.getWriteIndex()) / static_cast<float>(nFrames);

	ImGuiViewport* vp = ImGui::GetMainViewport();
	float vx = vp ? vp->Pos.x : 0.0f;
	float vy = vp ? vp->Pos.y : 0.0f;
	float vw = vp ? vp->Size.x : ImGui::GetIO().DisplaySize.x;
	float vh = vp ? vp->Size.y : ImGui::GetIO().DisplaySize.y;

	float margin = 12.0f;
	float barH = 8.0f;
	float barW = vw - margin * 2.0f;
	float barX = vx + margin;
	float barY = vy + vh - margin - barH;

	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImU32 bgCol = IM_COL32(30, 30, 35, 200);
	ImU32 borderCol = IM_COL32(80, 80, 90, 255);
	dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), bgCol, 3.0f);
	dl->AddRect(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), borderCol, 3.0f, 0, 1.5f);

	float rx = barX + readNorm * barW;
	float wx = barX + writeNorm * barW;
	ImU32 readCol = IM_COL32(0, 220, 255, 255);
	ImU32 writeCol = IM_COL32(255, 80, 80, 255);
	dl->AddLine(ImVec2(rx, static_cast<float>(barY - 2)), ImVec2(rx, static_cast<float>(barY + barH + 2)), readCol, 2.0f);
	dl->AddLine(ImVec2(wx, static_cast<float>(barY - 2)), ImVec2(wx, static_cast<float>(barY + barH + 2)), writeCol, 2.0f);
}


