#include "App.hpp"

#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "gl/Shader.hpp"
#include "gl/FullscreenQuad.hpp"
#include "gl/FxPipeline.hpp"
#include "util/Repeat.hpp"
#include "buffer/RingBuffer.hpp"
#include "looper/LooperEngine.hpp"
#include "util/HotReload.hpp"
#include "ui/ParamBus.hpp"
#include "ui/ControlsPanel.hpp"
#include "clock/Clock.hpp"
#include "io/VideoSource.hpp"
#include "io/FolderList.hpp"
#include "io/SourceController.hpp"
#include "io/VideoThumbs.hpp"
#include "link/LinkBridge.hpp"
#include "util/ContainRect.hpp"
#include "util/GlUtils.hpp"
#include "io/AVFRecorder.hpp"
#include "remote/RemoteApply.hpp"
#include "remote/RemoteSession.hpp"
#include "setup/SetupStore.hpp"
#include <cstdlib>
#include <vector>

static void glfwErrorCallback(int code, const char* desc) {
	std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

int App::run() {
	glfwSetErrorCallback(glfwErrorCallback);
	if (!glfwInit()) {
		std::fprintf(stderr, "Failed to initialize GLFW\n");
		return 1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
	glfwWindowHint(GLFW_REFRESH_RATE, 60);

	GLFWwindow* window = glfwCreateWindow(1280, 720, "Video Looper", nullptr, nullptr);
	if (!window) {
		std::fprintf(stderr, "Failed to create window\n");
		glfwTerminate();
		return 1;
	}

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	std::printf("OpenGL version: %s\n", glGetString(GL_VERSION));

	GLFWwindow* previewWindow = nullptr;
	FullscreenQuad* quadPreview = nullptr;

	int fbw, fbh;
	glfwGetFramebufferSize(window, &fbw, &fbh);
	RingBuffer ring;
	ring.setup(fbw, fbh, 120);
	LooperEngine looper(ring);
	looper.init(fbw, fbh);
	SourceController source;
	source.init(fbw, fbh);
	FxPipeline fx;
	FxPipeline sourceFx;
	// Audition render targets
	GLuint auditionTexA = 0, auditionTexB = 0;
	GLuint auditionFboA = 0, auditionFboB = 0;

	ShaderProgram shaderTest;
	ShaderProgram shaderBlit;
	ShaderProgram shaderFit;
	ShaderProgram shaderBlendTwo;
	std::string vsPath = std::string(SHADER_DIR) + "/fullscreen.vert";
	std::string testFragPath = std::string(SHADER_DIR) + "/test_pattern.frag";
	std::string blitFragPath = std::string(SHADER_DIR) + "/texture_blit.frag";
	std::string fitFragPath = std::string(SHADER_DIR) + "/video_fit.frag";
	std::string blendTwoFragPath = std::string(SHADER_DIR) + "/texture_blend_two.frag";
	std::string feedbackFragPath = std::string(SHADER_DIR) + "/feedback.frag";
	std::string feedbackDiffFragPath = std::string(SHADER_DIR) + "/feedback_diff.frag";
	std::string feedbackChromaFragPath = std::string(SHADER_DIR) + "/feedback_chroma.frag";
	std::string feedbackKaleidoFragPath = std::string(SHADER_DIR) + "/feedback_kaleido.frag";
	std::string blurXFragPath = std::string(SHADER_DIR) + "/fx_blur_x.frag";
	std::string blurYFragPath = std::string(SHADER_DIR) + "/fx_blur_y.frag";
	std::string repeatFragPath = std::string(SHADER_DIR) + "/fx_repeat.frag";
	shaderTest.loadFromFiles(vsPath, testFragPath);
	shaderBlit.loadFromFiles(vsPath, blitFragPath);
	shaderFit.loadFromFiles(vsPath, fitFragPath);
	shaderBlendTwo.loadFromFiles(vsPath, blendTwoFragPath);
	fx.init(fbw, fbh, vsPath, feedbackFragPath, feedbackDiffFragPath, feedbackChromaFragPath, feedbackKaleidoFragPath,
	        blurXFragPath, blurYFragPath, repeatFragPath);
	sourceFx.init(fbw, fbh, vsPath, feedbackFragPath, feedbackDiffFragPath, feedbackChromaFragPath,
	              feedbackKaleidoFragPath, blurXFragPath, blurYFragPath, repeatFragPath);

	// Create audition targets
	auditionTexA = createTexture2D(fbw, fbh);
	auditionFboA = createFbo(auditionTexA);
	auditionTexB = createTexture2D(fbw, fbh);
	auditionFboB = createFbo(auditionTexB);

	HotReload hot;
	hot.watch(vsPath);
	hot.watch(testFragPath);
	hot.watch(blitFragPath);
	hot.watch(fitFragPath);
	hot.watch(blendTwoFragPath);
	fx.registerWatches(hot);
	FullscreenQuad quad;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	ImGui::StyleColorsDark();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 410");

	ParamBus params;
	params.setBool("show_ui", true);
	params.setFloat("gain_mix", 1.0f);
	params.setFloat("record_blend_opacity", 1.0f);
	params.setFloat("bpm", 120.0f);
	params.setFloat("quantum", 4.0f);
	params.setFloat("read_speed_mult", 1.0f);
	params.setBool("tempo_coupled", true);
	params.setBool("link_enabled", false);
	params.setBool("link_startstop", false);
	params.setBool("link_playing", false);
	params.setBool("arm_record", false);
	params.setBool("show_preview", true);
	params.setBool("preview_window", false);
	params.setFloat("preview_scale", 0.3f);
	params.setInt("source_kind", (int)SourceKind::Shader);
	params.setBool("video_bars", true);
	params.setBool("video_aspect", true);
	params.setFloat("video_tx", 0.0f);
	params.setFloat("video_ty", 0.0f);
	params.setFloat("video_sx", 1.0f);
	params.setFloat("video_sy", 1.0f);
	params.setFloat("video_rot", 0.0f);
	params.setInt("record_blend_mode", 0);
	params.setInt("webcam_index", 0);
	params.setString("file_path", "");
	params.setBool("file_loop", true);
	params.setBool("file_paused", false);
	params.setInt("file_run_mode", 0);
	params.setFloat("file_loop_start", 0.0f);
	params.setFloat("file_loop_length", 1.0f);
	params.setBool("show_playheads", true);
	params.setBool("debug_preview_backbuffer", false);
	params.setString("videos_folder", "");
	params.setInt("videos_count", 0);
	params.setBool("commit_random", false);
	params.setBool("commit_active", false);
	params.setString("commit_current_file", "");
	params.setBool("fx_enabled", false);
	params.setBool("fx_commit", false);
	params.setInt("fx_mode", 0);
	params.setFloat("fx_param1", 0.8f);
	params.setFloat("fx_param2", 0.985f);
	params.setFloat("fx_param3", 0.0f);
	params.setInt("fx_count", 4);
	params.setInt("fx_repeat_blend", 6);
	params.setBool("source_fx_enabled", false);
	params.setInt("source_fx_mode", kRepeatFxMode);
	params.setFloat("source_fx_param1", 0.12f);
	params.setFloat("source_fx_param2", 0.0f);
	params.setFloat("source_fx_param3", 0.0f);
	params.setInt("source_fx_count", 4);
	params.setInt("source_fx_repeat_blend", 6);
	params.setBool("env_enabled", true);
	params.setFloat("env_attack", 0.05f);
	params.setFloat("env_decay", 0.20f);
	params.setFloat("env_sustain", 1.0f);
	params.setFloat("env_release", 0.30f);
	params.setBool("cue_active", false);
	params.setBool("record_enabled", false);
	params.setFloat("record_fps", 60.0f);
	params.setString("record_path", "");
	params.setBool("ipad_page", false);
	params.setBool("ipad_token", true);
	params.setFloat("silence_ms", 300.0f);

	const char* home = std::getenv("HOME");
	std::filesystem::path support =
		std::filesystem::path(home ? home : ".") / "Library/Application Support/video-looper";
	RemoteSession remote(support / "remote-token");
	remote.setUsageFile(support / "web-use.jsonl");
	std::string remoteSnapshotSent;
	VideoThumbs thumbs;
	std::vector<std::string> videoPaths;
	std::vector<std::string> videoNames;
	std::vector<std::string> sentVideoNames;
	std::vector<std::string> sentFolderPaths;
	FolderList folders;
	SetupSaver setupSaver(support / "setup.json", 0.2);
	{
		PerformanceSetup setup;
		if (readSetup(support / "setup.json", setup)) {
			applySetup(params, setup);
			RestoredFolders restored = restoreFolders(setup);
			for (const std::string& folder : restored.paths)
				folders.add({folder, scanVideos(folder)});
			if (!restored.current.empty() && folders.select(restored.current) && folders.current())
				source.showFolder(restored.current, folders.current()->videos, params);
			else {
				params.setString("videos_folder", "");
				params.setInt("videos_count", 0);
			}
		}
		if (params.getInt("source_kind", (int)SourceKind::Shader) == (int)SourceKind::Webcam)
			source.loadWebcam(params.getInt("webcam_index", 0));
		setupSaver.settle(captureSetup(params, folders));
	}
	std::string sourceFailure;

	Clock clock;
	clock.setQuantum(params.getFloat("quantum", 4.0f));
	LinkBridge link;
	AVFRecorder recorder;
	std::vector<unsigned char> readBuf;
	std::vector<unsigned char> flipBuf;
	double recordAccumSeconds = 0.0;
	double recordFramePeriod = 1.0 / 60.0;
	auto ensureBuffers = [&](int width, int height) {
		const size_t sz = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
		if (readBuf.size() != sz) readBuf.assign(sz, 0);
		if (flipBuf.size() != sz) flipBuf.assign(sz, 0);
	};
	ensureBuffers(fbw, fbh);

	float readFps = 60.0f;
	double lastTime = glfwGetTime();

	// Hotkey state (for edge-triggered actions)
	bool prevU = false;
	bool prevMinus = false, prevEqual = false;
	bool prevLBracket = false, prevRBracket = false;
	bool prevApostrophe = false, prevBackslash = false;
	bool prevV = false;
	bool prev1 = false, prev2 = false, prev3 = false, prev4 = false, prev5 = false;
	bool prev6 = false, prev7 = false, prev8 = false, prev9 = false, prev0 = false;
	bool shouldRecordOnRelease = false;

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		// Global hotkeys (skip when ImGui wants keyboard to avoid interfering with text inputs)
		{
			ImGuiIO& iof = ImGui::GetIO();
			// bool wantKb = iof.WantCaptureKeyboard;
			bool wantKb = false;
			auto keyDown = [&](int key) { return glfwGetKey(window, key) == GLFW_PRESS; };
			auto edgePressed = [&](int key, bool& prev) {
				bool d = keyDown(key);
				bool jp = d && !prev;
				prev = d;
				return jp;
			};
			if (!wantKb) {
				// Toggle UI
				if (edgePressed(GLFW_KEY_U, prevU)) {
					bool show = params.getBool("show_ui", true);
					params.setBool("show_ui", !show);
				}
				// Set Blend Gain via number keys (1..9 => 0.1..0.9, 0 => 1.0)
				if (edgePressed(GLFW_KEY_1, prev1)) params.setFloat("record_blend_opacity", 0.1f);
				if (edgePressed(GLFW_KEY_2, prev2)) params.setFloat("record_blend_opacity", 0.2f);
				if (edgePressed(GLFW_KEY_3, prev3)) params.setFloat("record_blend_opacity", 0.3f);
				if (edgePressed(GLFW_KEY_4, prev4)) params.setFloat("record_blend_opacity", 0.4f);
				if (edgePressed(GLFW_KEY_5, prev5)) params.setFloat("record_blend_opacity", 0.5f);
				if (edgePressed(GLFW_KEY_6, prev6)) params.setFloat("record_blend_opacity", 0.6f);
				if (edgePressed(GLFW_KEY_7, prev7)) params.setFloat("record_blend_opacity", 0.7f);
				if (edgePressed(GLFW_KEY_8, prev8)) params.setFloat("record_blend_opacity", 0.8f);
				if (edgePressed(GLFW_KEY_9, prev9)) params.setFloat("record_blend_opacity", 0.9f);
				if (edgePressed(GLFW_KEY_0, prev0)) params.setFloat("record_blend_opacity", 1.0f);
				// Prev/Next input source (- / =)
				if (edgePressed(GLFW_KEY_MINUS, prevMinus)) {
					int s = params.getInt("source_kind", (int)SourceKind::Shader);
					if (s > (int)SourceKind::Shader) params.setInt("source_kind", s - 1);
				}
				if (edgePressed(GLFW_KEY_EQUAL, prevEqual)) {
					int s = params.getInt("source_kind", (int)SourceKind::Shader);
					if (s < (int)SourceKind::Black) params.setInt("source_kind", s + 1);
				}
				// Prev/Next record blend mode ([ / ])
				if (edgePressed(GLFW_KEY_LEFT_BRACKET, prevLBracket)) {
					int m = params.getInt("record_blend_mode", 0);
					if (m > 0) params.setInt("record_blend_mode", m - 1);
				}
				if (edgePressed(GLFW_KEY_RIGHT_BRACKET, prevRBracket)) {
					int m = params.getInt("record_blend_mode", 0);
					if (m < 11) params.setInt("record_blend_mode", m + 1);
				}
				// Prev/Next FX mode (' / \)
				if (edgePressed(GLFW_KEY_APOSTROPHE, prevApostrophe)) {
					int m = params.getInt("fx_mode", 0);
					if (m > 0) params.setInt("fx_mode", m - 1);
				}
				if (edgePressed(GLFW_KEY_BACKSLASH, prevBackslash)) {
					int m = params.getInt("fx_mode", 0);
					if (m < kGridFxMode) params.setInt("fx_mode", m + 1);
				}
				// Load Random File (v)
				if (edgePressed(GLFW_KEY_V, prevV)) {
					source.triggerLoadRandomFile(params);
				}
			}
		}
		if (params.getBool("preview_window", false)) {
			if (!previewWindow) {
				previewWindow = glfwCreateWindow(640, 360, "Preview", nullptr, window);
				if (previewWindow) {
					glfwMakeContextCurrent(previewWindow);
					glfwSwapInterval(1);
					quadPreview = new FullscreenQuad();
					glfwMakeContextCurrent(window);
				}
			}
		} else {
			if (previewWindow) {
				glfwMakeContextCurrent(previewWindow);
				if (quadPreview) {
					delete quadPreview;
					quadPreview = nullptr;
				}
				glfwDestroyWindow(previewWindow);
				previewWindow = nullptr;
				glfwMakeContextCurrent(window);
			}
		}

		int w, h;
		glfwGetFramebufferSize(window, &w, &h);
		if (w != fbw || h != fbh) {
			fbw = w;
			fbh = h;
			ring.setup(fbw, fbh, 120);
			source.resize(fbw, fbh);
			fx.resize(fbw, fbh);
			sourceFx.resize(fbw, fbh);
			looper.resize(fbw, fbh);
			// Recreate audition targets
			if (auditionFboA) glDeleteFramebuffers(1, &auditionFboA);
			if (auditionTexA) glDeleteTextures(1, &auditionTexA);
			if (auditionFboB) glDeleteFramebuffers(1, &auditionFboB);
			if (auditionTexB) glDeleteTextures(1, &auditionTexB);
			auditionTexA = createTexture2D(fbw, fbh);
			auditionFboA = createFbo(auditionTexA);
			auditionTexB = createTexture2D(fbw, fbh);
			auditionFboB = createFbo(auditionTexB);
			ensureBuffers(fbw, fbh);
			if (recorder.isRecording()) {
				recorder.stop();
				params.setBool("record_enabled", false);
			}
		}
		glViewport(0, 0, w, h);

		double now = glfwGetTime();
		float t = static_cast<float>(now);
		float dt = static_cast<float>(now - lastTime);
		lastTime = now;

		if (hot.changed(vsPath) || hot.changed(testFragPath)) shaderTest.loadFromFiles(vsPath, testFragPath);
		if (hot.changed(vsPath) || hot.changed(blitFragPath)) shaderBlit.loadFromFiles(vsPath, blitFragPath);
		if (hot.changed(vsPath) || hot.changed(fitFragPath)) shaderFit.loadFromFiles(vsPath, fitFragPath);
		if (hot.changed(vsPath) || hot.changed(blendTwoFragPath))
			shaderBlendTwo.loadFromFiles(vsPath, blendTwoFragPath);
		sourceFx.applyReload(fx.hotReload(hot));

		// Manage recording state edge transitions
		static bool prevRecEnabled = false;
		bool recEnabled = params.getBool("record_enabled", false);
		if (recEnabled && !prevRecEnabled) {
			std::string outPath = params.getString("record_path", "");
			double rfps = params.getFloat("record_fps", 60.0f);
			if (!outPath.empty()) {
				recordFramePeriod = (rfps > 0.0 ? (1.0 / rfps) : (1.0 / 60.0));
				recordAccumSeconds = 0.0;
				std::printf("[rec] start %dx%d @ %.2f fps -> %s\n", fbw, fbh, rfps, outPath.c_str());
				bool ok = recorder.start(outPath, fbw, fbh, rfps);
				if (!ok) {
					std::fprintf(stderr, "[rec] failed to start writer\n");
					params.setBool("record_enabled", false);
				}
			} else {
				params.setBool("record_enabled", false);
			}
		}
		if (!recEnabled && prevRecEnabled) {
			std::printf("[rec] stop\n");
			recorder.stop();
		}
		prevRecEnabled = recEnabled;

		source.tickAndGrab(now, dt, params);
		int srcKind = params.getInt("source_kind", (int)SourceKind::Shader);
		if (srcKind == (int)SourceKind::Shader) {
			glBindFramebuffer(GL_FRAMEBUFFER, source.getPreviewFbo());
			glViewport(0, 0, fbw, fbh);
			shaderTest.use();
			shaderTest.setUniform("uTime", t);
			shaderTest.setUniform("uResolution", static_cast<float>(fbw), static_cast<float>(fbh));
			quad.draw();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		} else if (srcKind == (int)SourceKind::Black) {
			glBindFramebuffer(GL_FRAMEBUFFER, source.getPreviewFbo());
			glViewport(0, 0, fbw, fbh);
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		} else if ((srcKind == (int)SourceKind::File || srcKind == (int)SourceKind::Webcam) &&
		           source.frameTexture() != 0 && source.frameWidth() > 0 && source.frameHeight() > 0) {
			bool bars = params.getBool("video_bars", true);
			bool aspect = params.getBool("video_aspect", true);
			float sx = params.getFloat("video_sx", 1.0f);
			float sy = aspect ? sx : params.getFloat("video_sy", 1.0f);
			float tx = params.getFloat("video_tx", 0.0f);
			float ty = params.getFloat("video_ty", 0.0f);
			float rot = params.getFloat("video_rot", 0.0f);
			float baseW = static_cast<float>(fbw);
			float baseH = static_cast<float>(fbh);
			if (aspect) {
				ContainRect fitted = containRect(source.frameWidth(), source.frameHeight(), fbw, fbh);
				baseW = static_cast<float>(fitted.w);
				baseH = static_cast<float>(fitted.h);
			}
			glBindFramebuffer(GL_FRAMEBUFFER, source.getPreviewFbo());
			glViewport(0, 0, fbw, fbh);
			shaderFit.use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, source.frameTexture());
			shaderFit.setUniform("uTex", 0);
			shaderFit.setUniform("uFrame", static_cast<float>(fbw), static_cast<float>(fbh));
			shaderFit.setUniform("uCenter", (0.5f + tx) * fbw, (0.5f + ty) * fbh);
			shaderFit.setUniform("uHalf", baseW * 0.5f * sx, baseH * 0.5f * sy);
			shaderFit.setUniform("uRot", rot);
			shaderFit.setUniform("uOutside", bars ? 1.0f : 0.0f);
			quad.draw();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}

		float bpm = params.getFloat("bpm", 120.0f);
		float quantum = params.getFloat("quantum", 4.0f);
		clock.setQuantum(quantum);
		bool linkEnabled = params.getBool("link_enabled", false);
		link.setEnabled(linkEnabled);
		link.setQuantum(quantum);
		link.tick(dt);
		if (linkEnabled) {
			link.setStartStopSyncEnabled(params.getBool("link_startstop", false));
			bool lp = params.getBool("link_playing", false);
			link.setPlaying(lp);
			double beats, phase, lbpm;
			link.getBeatTime(quantum, beats, phase, lbpm);
			bpm = static_cast<float>(lbpm);
			params.setFloat("bpm", bpm);
			clock.requestTempo(bpm);
		} else {
			clock.requestTempo(bpm);
		}
		BeatTime bt = clock.now(dt);
		if (params.getBool("tempo_coupled", false))
			readFps = 60.0f * static_cast<float>(bt.bpm / 120.0) * params.getFloat("read_speed_mult", 1.0f);
		else
			readFps = 60.0f * params.getFloat("read_speed_mult", 1.0f);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, w, h);
		glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		ReadState rs = looper.advanceRead(dt, readFps);
		int idx0 = rs.idx0, idx1 = rs.idx1, wraps = rs.wraps;
		float mixf = rs.mix;
		float gainMix = params.getFloat("gain_mix", 1.0f);

		remote.setTokenRequired(params.getBool("ipad_token", true));
		remote.setEnabled(params.getBool("ipad_page", false));
		std::vector<std::string> videos = source.videos();
		if (videos != videoPaths) {
			videoPaths = std::move(videos);
			videoNames.clear();
			for (const std::string& p : videoPaths)
				videoNames.push_back(std::filesystem::path(p).filename());
			thumbs.setVideos(videoPaths);
		}
		std::vector<std::pair<std::string, std::string>> madeThumbs;
		if (thumbs.take(madeThumbs)) remote.setThumbs(std::move(madeThumbs));
		std::string loadFailure = source.takeLoadFailure();
		if (!loadFailure.empty())
			sourceFailure = loadFailure;
		else if (source.loadSwapped() || (srcKind != (int)SourceKind::File && srcKind != (int)SourceKind::Webcam))
			sourceFailure.clear();
		if (remote.enabled()) {
			remote.setSilenceWindow(int(params.getFloat("silence_ms", 300.0f)));
			if (!loadFailure.empty()) remote.send(rejectFrame("source", loadFailure));
			for (const RemoteSet& set : remote.drain()) {
				switch (set.kind) {
				case RemoteKind::Panic:
					params.setBool("arm_record", false);
					params.setBool("cue_active", false);
					break;
				case RemoteKind::Prev:
					source.loadPrevFile(params);
					break;
				case RemoteKind::Next:
					source.loadNextFile(params);
					break;
				case RemoteKind::Webcam:
					source.loadWebcam(params.getInt("webcam_index", 0));
					break;
				case RemoteKind::Load: {
					std::string path = videoByName(videoPaths, set.name);
					if (path.empty())
						remote.send(rejectFrame("source", "no such video"));
					else
						source.loadVideo(path, params.getFloat("file_loop_start", 0.0f),
						                 params.getFloat("file_loop_length", 1.0f));
					break;
				}
				case RemoteKind::Dub: {
					std::string path = videoByName(videoPaths, set.name);
					if (path.empty())
						remote.send(rejectFrame("source", "no such video"));
					else
						source.queueDub(path, params.getFloat("file_loop_start", 0.0f),
						                params.getFloat("file_loop_length", 1.0f));
					break;
				}
				case RemoteKind::Page:
					source.primePage(videosByName(videoPaths, set.names), params.getFloat("file_loop_start", 0.0f),
					                 params.getFloat("file_loop_length", 1.0f));
					break;
				case RemoteKind::Folder:
					if (folders.select(set.name))
						source.showFolder(set.name, folders.current()->videos, params);
					else
						remote.send(rejectFrame("folder", "no such folder"));
					break;
				case RemoteKind::Set: {
					ApplyResult result = applyRemoteSet(params, set);
					if (result.reject) remote.send(rejectFrame(set.name, result.reason));
					if (result.tempoChanged && params.getBool("link_enabled", false)) link.setTempo(result.bpm);
					if (result.startWebcam) source.loadWebcam(params.getInt("webcam_index", 0));
					break;
				}
				}
			}
		} else {
			params.setBool("ipad_page", false);
		}
		RemoteSession::PadState pads = remote.pads();

		// Compute record/cue and envelope
		static float envVal = 0.0f;
		static int envPhase = 0;
		static bool gatePrev = false;
		bool commitActive = params.getBool("commit_active", false);
		bool spaceHeld = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
		bool cHeld = (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS);
		GateState combine = combineHolds({params.getBool("arm_record", false), spaceHeld, pads.gate,
		                                  params.getBool("cue_active", false), cHeld, pads.cue});
		bool cueActive = combine.cue;
		bool recGate = combine.gate;

		if (cueActive) shouldRecordOnRelease = false;
		if (recGate) shouldRecordOnRelease = true;

		bool envEnabled = params.getBool("env_enabled", true);
		float A = params.getFloat("env_attack", 0.05f);
		if (A < 0.0f) A = 0.0f;
		float D = params.getFloat("env_decay", 0.20f);
		if (D < 0.0f) D = 0.0f;
		float S = params.getFloat("env_sustain", 1.0f);
		if (S < 0.0f) S = 0.0f;
		if (S > 1.0f) S = 1.0f;
		float R = params.getFloat("env_release", 0.30f);
		if (R < 0.0f) R = 0.0f;
		if (!commitActive && envEnabled) {
			if ((recGate || cueActive) && !gatePrev) {
				envPhase = 1;
			}
			if (!(recGate || cueActive) && gatePrev && envPhase != 0) {
				envPhase = 4;
			}
			switch (envPhase) {
			case 0:
				envVal = 0.0f;
				break;
			case 1:
				if (A <= 0.0f) {
					envVal = 1.0f;
					envPhase = (D > 0.0f) ? 2 : 3;
				} else {
					envVal += dt / A;
					if (envVal >= 1.0f) {
						envVal = 1.0f;
						envPhase = (D > 0.0f) ? 2 : 3;
					}
				}
				break;
			case 2:
				if (D <= 0.0f) {
					envVal = S;
					envPhase = 3;
				} else {
					float step = (1.0f - S) * (dt / D);
					envVal -= step;
					if (envVal <= S) {
						envVal = S;
						envPhase = 3;
					}
				}
				break;
			case 3:
				envVal = S;
				if (!(recGate || cueActive)) envPhase = 4;
				break;
			case 4:
				if (R <= 0.0f) {
					envVal = 0.0f;
					envPhase = 0;
				} else {
					envVal -= dt / (R <= 1e-6f ? 1e-6f : R);
					if (envVal <= 0.0f) {
						envVal = 0.0f;
						envPhase = 0;
					}
				}
				break;
			}
		}
		// On rising edge of record or cue, optionally trigger file playback
		if ((recGate || cueActive) && !gatePrev) {
			if (params.getInt("file_run_mode", 0) == 1) {
				if (params.getInt("source_kind", (int)SourceKind::Shader) == (int)SourceKind::File) {
					source.restartFileAtLoopStart();
				}
			}
		}
		gatePrev = recGate || cueActive;

		float writeOpacity = params.getFloat("record_blend_opacity", 1.0f) *
		                     (commitActive || !envEnabled ? ((recGate || commitActive) ? 1.0f : 0.0f) : envVal);
		bool fxCommit = params.getBool("fx_commit", false) && params.getBool("fx_enabled", false);
		int recordBlendMode = params.getInt("record_blend_mode", 0);
		// bool recordingActive = commitActive || (envEnabled ? (recGate && (envVal > 0.0f)) : recGate);

		bool recordingActive = commitActive || (envEnabled ? (shouldRecordOnRelease && (envVal > 0.0f)) : recGate);

		GLuint liveSource = source.getPreviewTexture();
		if (params.getBool("source_fx_enabled", false) && liveSource != 0) {
			int mode = params.getInt("source_fx_mode", kRepeatFxMode);
			float p1 = params.getFloat("source_fx_param1", 0.12f);
			float p2 = params.getFloat("source_fx_param2", 0.0f);
			float p3 = params.getFloat("source_fx_param3", 0.0f);
			if (fxStampsCopies(mode)) p3 = static_cast<float>(params.getInt("source_fx_count", 4));
			int repeatBlend = params.getInt("source_fx_repeat_blend", 6);
			liveSource = sourceFx.process(fbw, fbh, liveSource, liveSource, 0.0f, mode, p1, p2, p3, repeatBlend, quad);
		}

		// Perform writes unless cueing (monitor only)
		if (!cueActive) {
			looper.writeAndSwap(fbw, fbh, shaderBlit, shaderBlendTwo, quad, idx0, wraps, recordingActive, writeOpacity,
			                    recordBlendMode, fxCommit, fx.getPrevOutputTexture(), liveSource);
		}

		// Present audition / normal
		if (cueActive) {
			// Pass 1: front-bank loop composite -> A
			glBindFramebuffer(GL_FRAMEBUFFER, auditionFboA);
			glViewport(0, 0, fbw, fbh);
			shaderBlendTwo.use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, ring.textureAt(idx0));
			shaderBlendTwo.setUniform("uTex0", 0);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, ring.textureAt(idx1));
			shaderBlendTwo.setUniform("uTex1", 1);
			shaderBlendTwo.setUniform("uMix", mixf * gainMix);
			shaderBlendTwo.setUniform("uMode", 0);
			quad.draw();

			// Pass 2: overlay preview with record blend -> B
			glBindFramebuffer(GL_FRAMEBUFFER, auditionFboB);
			glViewport(0, 0, fbw, fbh);
			shaderBlendTwo.use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, auditionTexA);
			shaderBlendTwo.setUniform("uTex0", 0);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, liveSource);
			shaderBlendTwo.setUniform("uTex1", 1);
			shaderBlendTwo.setUniform("uMix", writeOpacity);
			shaderBlendTwo.setUniform("uMode", recordBlendMode);
			quad.draw();

			// Present B
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, w, h);
			shaderBlit.use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, auditionTexB);
			shaderBlit.setUniform("uTex", 0);
			quad.draw();
		} else if (recordingActive) {
			// Show audition composite while recording
			// Pass 1: front-bank loop composite -> A
			glBindFramebuffer(GL_FRAMEBUFFER, auditionFboA);
			glViewport(0, 0, fbw, fbh);
			shaderBlendTwo.use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, ring.textureAt(idx0));
			shaderBlendTwo.setUniform("uTex0", 0);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, ring.textureAt(idx1));
			shaderBlendTwo.setUniform("uTex1", 1);
			shaderBlendTwo.setUniform("uMix", mixf * gainMix);
			shaderBlendTwo.setUniform("uMode", 0);
			quad.draw();

			// Pass 2: overlay preview with record blend -> B
			glBindFramebuffer(GL_FRAMEBUFFER, auditionFboB);
			glViewport(0, 0, fbw, fbh);
			shaderBlendTwo.use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, auditionTexA);
			shaderBlendTwo.setUniform("uTex0", 0);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, liveSource);
			shaderBlendTwo.setUniform("uTex1", 1);
			shaderBlendTwo.setUniform("uMix", writeOpacity);
			shaderBlendTwo.setUniform("uMode", recordBlendMode);
			quad.draw();

			// Present B
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, w, h);
			shaderBlit.use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, auditionTexB);
			shaderBlit.setUniform("uTex", 0);
			quad.draw();
		} else {
			bool fxEnabled = params.getBool("fx_enabled", false);
			if (fxEnabled) {
				int fxMode = params.getInt("fx_mode", 0);
				float p1p = params.getFloat("fx_param1", 0.8f);
				float p2p = params.getFloat("fx_param2", 0.985f);
				float p3p = params.getFloat("fx_param3", 0.0f);
				if (fxStampsCopies(fxMode)) p3p = static_cast<float>(params.getInt("fx_count", 4));
				int repeatBlend = params.getInt("fx_repeat_blend", 6);
				float baseMix = mixf * gainMix;
				GLuint fxOut = fx.process(w, h, ring.textureAt(idx0), ring.textureAt(idx1), baseMix, fxMode, p1p, p2p,
				                          p3p, repeatBlend, quad);
				glViewport(0, 0, w, h);
				shaderBlit.use();
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, fxOut);
				shaderBlit.setUniform("uTex", 0);
				quad.draw();
			} else {
				looper.presentNoFx(w, h, shaderBlendTwo, quad, idx0, idx1, mixf, gainMix);
			}
		}

		// Build audition composite for preview (skip during cue/record; not envelope-influenced)
		{
			bool previewVisible = (params.getBool("show_preview", true) && !params.getBool("preview_window", false)) ||
			                      params.getBool("preview_window", false);

			if (previewVisible && !cueActive && !recordingActive) {
				float previewOpacity = params.getFloat("record_blend_opacity", 1.0f);
				int recordBlendMode = params.getInt("record_blend_mode", 0);

				// Pass A: loop front-bank composite -> A
				glBindFramebuffer(GL_FRAMEBUFFER, auditionFboA);
				glViewport(0, 0, fbw, fbh);
				shaderBlendTwo.use();
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, ring.textureAt(idx0));
				shaderBlendTwo.setUniform("uTex0", 0);
				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, ring.textureAt(idx1));
				shaderBlendTwo.setUniform("uTex1", 1);
				shaderBlendTwo.setUniform("uMix", mixf * gainMix);
				shaderBlendTwo.setUniform("uMode", 0);
				quad.draw();

				// Pass B: overlay live preview with fixed opacity -> B
				glBindFramebuffer(GL_FRAMEBUFFER, auditionFboB);
				glViewport(0, 0, fbw, fbh);
				shaderBlendTwo.use();
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, auditionTexA);
				shaderBlendTwo.setUniform("uTex0", 0);
				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, liveSource);
				shaderBlendTwo.setUniform("uTex1", 1);
				shaderBlendTwo.setUniform("uMix", previewOpacity);
				shaderBlendTwo.setUniform("uMode", recordBlendMode);
				quad.draw();

				glBindFramebuffer(GL_FRAMEBUFFER, 0);
			}
		}

		// When recording is enabled, build the audition composite offscreen (if not already built)
		if (params.getBool("record_enabled", false)) {
			// Pass A: loop front-bank composite -> A
			glBindFramebuffer(GL_FRAMEBUFFER, auditionFboA);
			glViewport(0, 0, fbw, fbh);
			shaderBlendTwo.use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, ring.textureAt(idx0));
			shaderBlendTwo.setUniform("uTex0", 0);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, ring.textureAt(idx1));
			shaderBlendTwo.setUniform("uTex1", 1);
			shaderBlendTwo.setUniform("uMix", mixf * gainMix);
			shaderBlendTwo.setUniform("uMode", 0);
			quad.draw();
			// Pass B: overlay live preview with record blend -> B
			glBindFramebuffer(GL_FRAMEBUFFER, auditionFboB);
			glViewport(0, 0, fbw, fbh);
			shaderBlendTwo.use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, auditionTexA);
			shaderBlendTwo.setUniform("uTex0", 0);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, liveSource);
			shaderBlendTwo.setUniform("uTex1", 1);
			shaderBlendTwo.setUniform("uMix", params.getFloat("record_blend_opacity", 1.0f));
			shaderBlendTwo.setUniform("uMode", params.getInt("record_blend_mode", 0));
			quad.draw();
		}

		// Capture the audition composite into the recorder at target FPS
		if (recorder.isRecording()) {
			recordAccumSeconds += dt;
			while (recordAccumSeconds + 1e-6 >= recordFramePeriod) {
				recordAccumSeconds -= recordFramePeriod;
				std::printf("[rec] capture frame begin %dx%d\n", fbw, fbh);
				glBindFramebuffer(GL_FRAMEBUFFER, auditionFboB);
				glPixelStorei(GL_PACK_ALIGNMENT, 1);
				glReadPixels(0, 0, fbw, fbh, GL_BGRA, GL_UNSIGNED_BYTE, readBuf.data());
				GLenum err = glGetError();
				if (err != GL_NO_ERROR) {
					std::fprintf(stderr, "[rec] glReadPixels error: 0x%04x\n", err);
				}
				int stride = fbw * 4;
				for (int y = 0; y < fbh; ++y) {
					unsigned char* dst = flipBuf.data() + (fbh - 1 - y) * stride;
					unsigned char* src = readBuf.data() + y * stride;
					memcpy(dst, src, (size_t)stride);
				}
				std::printf("[rec] append start\n");
				bool ok = recorder.appendBGRAFrame(flipBuf.data(), stride);
				std::printf("[rec] append %s\n", ok ? "ok" : "fail");
				if (!ok) {
					std::fprintf(stderr, "[rec] append dropped (not ready)\n");
					break;
				}
			}
		}

		if (params.getBool("show_preview", true) && !params.getBool("preview_window", false)) {
			float scale = params.getFloat("preview_scale", 0.3f);
			int pw = static_cast<int>(w * scale);
			int ph = static_cast<int>(pw * (static_cast<float>(fbh) / static_cast<float>(fbw)));
			int margin = 12;
			int px = w - pw - margin;
			int py = margin;
			glViewport(px, py, pw, ph);
			shaderBlit.use();
			glActiveTexture(GL_TEXTURE0);
			if (params.getBool("debug_preview_backbuffer", false))
				glBindTexture(GL_TEXTURE_2D, ring.textureAtBack(idx0));
			else
				glBindTexture(GL_TEXTURE_2D, auditionTexB);
			shaderBlit.setUniform("uTex", 0);
			quad.draw();
			glViewport(0, 0, w, h);
		}

		if (previewWindow && !glfwWindowShouldClose(previewWindow)) {
			glfwMakeContextCurrent(previewWindow);
			int pw, ph;
			glfwGetFramebufferSize(previewWindow, &pw, &ph);
			glViewport(0, 0, pw, ph);
			glClearColor(0.07f, 0.07f, 0.08f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			shaderBlit.use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, auditionTexB);
			shaderBlit.setUniform("uTex", 0);
			if (quadPreview) quadPreview->draw();
			glfwSwapBuffers(previewWindow);
			glfwMakeContextCurrent(window);
		} else if (previewWindow && glfwWindowShouldClose(previewWindow)) {
			glfwMakeContextCurrent(previewWindow);
			if (quadPreview) {
				delete quadPreview;
				quadPreview = nullptr;
			}
			glfwDestroyWindow(previewWindow);
			previewWindow = nullptr;
			glfwMakeContextCurrent(window);
			params.setBool("preview_window", false);
		}

		if (remote.enabled()) {
			RemoteLive live{(float(idx0) + mixf) / float(ring.size()), envVal, envPhase, recGate, cueActive};
			std::vector<std::string> folderPaths = folders.paths();
			bool catalogChanged = videoNames != sentVideoNames || folderPaths != sentFolderPaths;
			std::string full = remoteSnapshot(params, live, videoNames, folderPaths, true, sourceFailure);
			remote.setSnapshot(full);
			std::string wire = catalogChanged ? full : remoteSnapshot(params, live, {}, {}, false, sourceFailure);
			if (wire != remoteSnapshotSent) {
				remoteSnapshotSent = wire;
				remote.send(wire);
			}
			if (catalogChanged) {
				sentVideoNames = videoNames;
				sentFolderPaths = std::move(folderPaths);
			}
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		{
			ControlsPanel panel;
			float outBpmDummy = 0.0f, outQuantumDummy = 0.0f;
			panel.draw(params, link, ring, dt, outBpmDummy, outQuantumDummy, &source, remote, folders);
		}
		if (params.getBool("show_playheads", true)) looper.drawHeadsOverlay(w, h);
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}

		setupSaver.observe(captureSetup(params, folders), now);
		glfwSwapBuffers(window);
	}

	setupSaver.flush();

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();
	// Destroy audition targets
	if (auditionFboA) glDeleteFramebuffers(1, &auditionFboA);
	if (auditionTexA) glDeleteTextures(1, &auditionTexA);
	if (auditionFboB) glDeleteFramebuffers(1, &auditionFboB);
	if (auditionTexB) glDeleteTextures(1, &auditionTexB);
	return 0;
}
