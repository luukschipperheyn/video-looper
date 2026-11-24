#pragma once

#include "buffer/RingBuffer.hpp"
#include "looper/LoopPlan.hpp"

class Compositor;

struct LoopWrite {
	bool cue = false;
	bool overdub = false;
	bool bake = false;
	float opacity = 0;
	int blendMode = 0;
	unsigned int image = 0;
	unsigned int replaceWith = 0;
};

struct LoopPicture {
	int idx0 = 0;
	int idx1 = 0;
	float mix = 0.f;
	unsigned int tex0 = 0;
	unsigned int tex1 = 0;
};

class LooperEngine {
public:
	explicit LooperEngine(RingBuffer& ringBuffer);

	void init();
	void resize();

	LoopPicture step(float dtSeconds, float readFps, const LoopWrite& write, Compositor& compositor);

	void drawHeadsOverlay(int viewportW, int viewportH) const;

private:
	void paint(const SlotFill& fill, Compositor& compositor, const LoopWrite& write);

	RingBuffer& ring;
	LoopMark mark;
};
