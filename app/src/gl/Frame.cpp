#include "gl/Compositor.hpp"

#include "buffer/RingBuffer.hpp"
#include "looper/LooperEngine.hpp"

void initFrame(int width, int height, const std::string& shaderDir, RingBuffer& ring, LooperEngine& looper,
               Compositor& picture, FxPipeline& program, FxPipeline& source) {
	ring.setup(width, height, 120);
	looper.init();
	picture.init(width, height, shaderDir);
	program.init(width, height, shaderDir);
	source.init(width, height, shaderDir);
}

void resizeFrame(int width, int height, RingBuffer& ring, LooperEngine& looper, Compositor& picture,
                 FxPipeline& program, FxPipeline& source) {
	ring.setup(width, height, 120);
	program.resize(width, height);
	source.resize(width, height);
	looper.resize();
	picture.resize(width, height);
}

void reloadShaders(HotReload& hot, Compositor& picture, FxPipeline& program, FxPipeline& source) {
	auto changed = hot.poll();
	picture.hotReload(changed);
	source.applyReload(program.hotReload(changed));
}
