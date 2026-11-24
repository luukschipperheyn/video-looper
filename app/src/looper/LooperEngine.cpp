#include "looper/LooperEngine.hpp"

#include <cmath>
#include "imgui.h"

LooperEngine::LooperEngine(RingBuffer& ringBuffer) : ring(ringBuffer) {}

void LooperEngine::init() {
	mark = {};
}

void LooperEngine::resize() {
	const float read = mark.readFrame;
	mark = {};
	mark.readFrame = read;
}

void LooperEngine::paint(const SlotFill& fill, ShaderProgram& shaderBlit, ShaderProgram& shaderBlendTwo,
                         FullscreenQuad& quad, const LoopWrite& write) {
	const int width = ring.getWidth();
	const int height = ring.getHeight();
	if (width <= 0 || height <= 0) return;

	ring.bindWriteFboAt(fill.index);
	glViewport(0, 0, width, height);

	if (fill.action == SlotAction::Bake) {
		shaderBlit.use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, write.replaceWith);
		shaderBlit.setUniform("uTex", 0);
		quad.draw();
	} else if (fill.action == SlotAction::Overdub) {
		shaderBlendTwo.use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, ring.textureAt(fill.index));
		shaderBlendTwo.setUniform("uTex0", 0);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, write.image);
		shaderBlendTwo.setUniform("uTex1", 1);
		shaderBlendTwo.setUniform("uMix", write.opacity);
		shaderBlendTwo.setUniform("uMode", write.blendMode);
		quad.draw();
	} else {
		shaderBlit.use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, ring.textureAt(fill.index));
		shaderBlit.setUniform("uTex", 0);
		quad.draw();
	}
	ring.unbindWriteFbo();
}

LoopPicture LooperEngine::step(float dtSeconds, float readFps, ShaderProgram& shaderBlit, ShaderProgram& shaderBlendTwo,
                               FullscreenQuad& quad, const LoopWrite& write) {
	LoopWish wish;
	wish.cue = write.cue;
	wish.overdub = write.overdub;
	wish.bake = write.bake;
	const LoopPlan plan = planLoop(mark, ring.size(), dtSeconds, readFps, wish);

	LoopPicture picture;
	picture.idx0 = plan.read.idx0;
	picture.idx1 = plan.read.idx1;
	picture.mix = plan.read.mix;
	picture.tex0 = ring.textureAt(picture.idx0);
	picture.tex1 = ring.textureAt(picture.idx1);

	for (const BankWrite& bank : plan.banks) {
		for (const SlotFill& fill : bank.fills) paint(fill, shaderBlit, shaderBlendTwo, quad, write);
		if (bank.swap) ring.swapBanks();
	}
	return picture;
}

void LooperEngine::drawHeadsOverlay(int viewportW, int viewportH) const {
	int nFrames = ring.size();
	if (nFrames <= 0) return;
	float rf = std::fmod(mark.readFrame, static_cast<float>(nFrames));
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
	dl->AddLine(ImVec2(rx, static_cast<float>(barY - 2)), ImVec2(rx, static_cast<float>(barY + barH + 2)), readCol,
	            2.0f);
	dl->AddLine(ImVec2(wx, static_cast<float>(barY - 2)), ImVec2(wx, static_cast<float>(barY + barH + 2)), writeCol,
	            2.0f);
}
