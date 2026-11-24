#include "gl/Compositor.hpp"
#include "gl/FullscreenQuad.hpp"
#include "gl/FxPipeline.hpp"
#include "util/HotReload.hpp"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <utime.h>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) ++failures;
}

static GLuint currentProgram() {
	GLint id = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &id);
	return static_cast<GLuint>(id);
}

static GLuint drawFx(FxPipeline& fx, FullscreenQuad& quad) {
	GLuint tex = fx.getPrevOutputTexture();
	fx.process(tex, tex, 0.0f, FxSettings{}, quad);
	return currentProgram();
}

static bool shaderSourceHas(GLuint program, const char* marker) {
	GLint count = 0;
	glGetProgramiv(program, GL_ATTACHED_SHADERS, &count);
	if (count <= 0) return false;
	std::vector<GLuint> shaders(static_cast<size_t>(count));
	glGetAttachedShaders(program, count, nullptr, shaders.data());
	for (GLuint shader : shaders) {
		GLint length = 0;
		glGetShaderiv(shader, GL_SHADER_SOURCE_LENGTH, &length);
		if (length <= 0) continue;
		std::string src(static_cast<size_t>(length), '\0');
		glGetShaderSource(shader, length, nullptr, src.data());
		if (src.find(marker) != std::string::npos) return true;
	}
	return false;
}

static bool bumpMtime(const std::string& path) {
	struct stat st{};
	if (stat(path.c_str(), &st) != 0) return false;
	utimbuf times{};
	times.actime = st.st_atime;
	times.modtime = st.st_mtime + 2;
	return utime(path.c_str(), &times) == 0;
}

static bool editVertex(const std::string& path) {
	std::ifstream in(path);
	std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>{});
	in.close();
	if (src.empty()) return false;
	{
		std::ofstream out(path);
		out << src << "\n// reload-marker\n";
		if (!out) return false;
	}
	return bumpMtime(path);
}

struct OffscreenGl {
	CGLPixelFormatObj format = nullptr;
	CGLContextObj context = nullptr;

	bool open() {
		CGLPixelFormatAttribute attrs[] = {
			kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
			kCGLPFAAccelerated,   kCGLPFAColorSize,
			(CGLPixelFormatAttribute)24, kCGLPFAAlphaSize,
			(CGLPixelFormatAttribute)8,  (CGLPixelFormatAttribute)0};
		GLint count = 0;
		if (CGLChoosePixelFormat(attrs, &format, &count) != kCGLNoError || !format) return false;
		if (CGLCreateContext(format, nullptr, &context) != kCGLNoError || !context) return false;
		return CGLSetCurrentContext(context) == kCGLNoError;
	}

	~OffscreenGl() {
		CGLSetCurrentContext(nullptr);
		if (context) CGLDestroyContext(context);
		if (format) CGLReleasePixelFormat(format);
	}
};

int main() {
	OffscreenGl gl;
	if (!gl.open()) {
		std::printf("FAIL offscreen GL context\n");
		return 1;
	}

	namespace fs = std::filesystem;
	fs::path dir = fs::temp_directory_path() / "video-looper-shader-reload";
	std::error_code ec;
	fs::remove_all(dir, ec);
	fs::copy(SHADER_SRC, dir, fs::copy_options::recursive, ec);
	if (ec) {
		std::printf("FAIL copy shaders: %s\n", ec.message().c_str());
		return 1;
	}

	Compositor picture;
	FxPipeline program;
	FxPipeline source;
	FullscreenQuad quad;
	picture.init(16, 16, dir.string());
	program.init(16, 16, dir.string());
	source.init(16, 16, dir.string());

	HotReload hot;
	picture.registerWatches(hot);
	program.registerWatches(hot);

	GLuint programBefore = drawFx(program, quad);
	GLuint sourceBefore = drawFx(source, quad);
	check(programBefore != 0 && sourceBefore != 0 && picture.blitShader().id() != 0, "the FX programs are linked");
	check(editVertex((dir / "fullscreen.vert").string()), "the vertex shader changes");

	picture.hotReload(hot);
	check(shaderSourceHas(picture.blitShader().id(), "reload-marker"),
	      "the picture path reads the vertex shader edit");

	FxReload reload = program.hotReload(hot);
	source.applyReload(reload);
	check(shaderSourceHas(drawFx(program, quad), "reload-marker") &&
	          shaderSourceHas(drawFx(source, quad), "reload-marker"),
	      "a vertex-shader edit reloads the FX programs");

	fs::remove_all(dir, ec);
	return failures == 0 ? 0 : 1;
}
