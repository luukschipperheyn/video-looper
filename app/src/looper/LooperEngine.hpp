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

class LooperEngine {
public:
	explicit LooperEngine(RingBuffer& ringBuffer);

	void init(int width, int height);
	void resize(int width, int height);

	// Advances read position and computes indices/mix. Returns ReadState.
	ReadState advanceRead(float dtSeconds, float readFps);

	// Present read composite without FX to the default framebuffer
	void presentNoFx(int viewportW, int viewportH,
		ShaderProgram& shaderBlendTwo,
		FullscreenQuad& quad,
		int idx0, int idx1, float mix, float gainMix);

	// Perform multi-index write to back buffers, optionally committing FX or preview image
	void writeAndSwap(int fbw, int fbh,
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
		unsigned int previewTexture);

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


