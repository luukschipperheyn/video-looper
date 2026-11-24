#pragma once

#include <vector>

#include "buffer/RingBuffer.hpp"
#include "gl/Shader.hpp"
#include "gl/FullscreenQuad.hpp"

struct ReadState {
	int idx0;
	int idx1;
	float mix;
	int wraps;
};

struct LoopWrite {
	bool overdub = false;
	float opacity = 0;
	int blendMode = 0;
	unsigned int image = 0;
	unsigned int replaceWith = 0; // Bake FX: this texture replaces each slot. 0 for none.
};

class LooperEngine {
public:
	explicit LooperEngine(RingBuffer& ringBuffer);

	void init(int width, int height);
	void resize(int width, int height);

	// Advances read position and computes indices/mix. Returns ReadState.
	ReadState advanceRead(float dtSeconds, float readFps);

	// Brings the back bank up to the read position and swaps banks on a wrap. Each slot passed since the last
	// call is written once: with write.replaceWith when that is set, else with write.image blended over the
	// Loop when write.overdub is set, else copied from the front bank.
	void writeAndSwap(ShaderProgram& shaderBlit, ShaderProgram& shaderBlendTwo, FullscreenQuad& quad, int idx0,
	                  int wraps, const LoopWrite& write);

	// Draw playhead overlays using ImGui draw list
	void drawHeadsOverlay(int viewportW, int viewportH) const;

	float getReadFrame() const { return readFrame; }

private:
	RingBuffer& ring;
	int fbWidth;
	int fbHeight;
	float readFrame;
	int lastWriteIdx;
	std::vector<char> backWritten;
};
