#include "App.hpp"

#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "control/Hotkeys.hpp"
#include "control/Instrument.hpp"
#include "gl/Compositor.hpp"
#include "gl/FxPipeline.hpp"
#include "gl/PictureChoice.hpp"
#include "buffer/RingBuffer.hpp"
#include "looper/LooperEngine.hpp"
#include "looper/LoopFrame.hpp"
#include "looper/OverdubGate.hpp"
#include "util/HotReload.hpp"
#include "ui/ParamBus.hpp"
#include "ui/ControlsPanel.hpp"
#include "clock/Clock.hpp"
#include "io/DrawImage.hpp"
#include "io/VideoSource.hpp"
#include "io/FolderList.hpp"
#include "io/AVFDecoders.hpp"
#include "io/SourceController.hpp"
#include "io/VideoThumbs.hpp"
#include "link/LinkBridge.hpp"
#include "util/ContainRect.hpp"
#include "io/AVFRecorder.hpp"
#include "io/Export.hpp"
#include "remote/WebInterface.hpp"
#include "setup/SetupStore.hpp"
#include <cstdlib>
#include <vector>

static void glfwErrorCallback(int code, const char* desc) {
	std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

static SourcePlacement sourcePlacement(int frameW, int frameH, int srcW, int srcH, const ParamBus& params) {
	bool aspect = params.getBool("video_aspect");
	float sx = params.getFloat("video_sx");
	float sy = aspect ? sx : params.getFloat("video_sy");
	float baseW = static_cast<float>(frameW);
	float baseH = static_cast<float>(frameH);
	if (aspect) {
		ContainRect fitted = containRect(srcW, srcH, frameW, frameH);
		baseW = static_cast<float>(fitted.w);
		baseH = static_cast<float>(fitted.h);
	}
	float tx = params.getFloat("video_tx");
	float ty = params.getFloat("video_ty");
	return {(0.5f + tx) * frameW, (0.5f + ty) * frameH, baseW * 0.5f * sx, baseH * 0.5f * sy,
	        params.getFloat("video_rot")};
}

// prefix is "fx_" for Program FX and "source_fx_" for Source FX.
static FxSettings fxSettings(const ParamBus& params, const std::string& prefix) {
	return {params.getInt(prefix + "mode"),     params.getFloat(prefix + "param1"),
	        params.getFloat(prefix + "param2"), params.getFloat(prefix + "param3"),
	        params.getInt(prefix + "count"),    params.getInt(prefix + "repeat_blend")};
}

static GateSettings gateSettings(const ParamBus& params) {
	GateSettings out;
	out.envelope = params.getBool("env_enabled");
	out.attack = params.getFloat("env_attack");
	out.decay = params.getFloat("env_decay");
	out.sustain = params.getFloat("env_sustain");
	out.release = params.getFloat("env_release");
	out.opacity = params.getFloat("record_blend_opacity");
	out.blendMode = params.getInt("record_blend_mode");
	out.bakeFx = params.getBool("fx_commit") && params.getBool("fx_enabled");
	return out;
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

	RingBuffer ring;
	LooperEngine looper(ring);
	const std::string shaderDir = SHADER_DIR;
	Compositor compositor;
	FxPipeline fx;
	FxPipeline sourceFx;
	initFrame(kLoopWidth, kLoopHeight, shaderDir, ring, looper, compositor, fx, sourceFx);
	SourceController source(compositor, avfDecoders());

	HotReload hot;
	compositor.registerWatches(hot);
	fx.registerWatches(hot);

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

	const char* home = std::getenv("HOME");
	std::filesystem::path support =
		std::filesystem::path(home ? home : ".") / "Library/Application Support/video-looper";
	WebInterface web(support / "remote-token");
	web.setUsageFile(support / "web-use.jsonl");
	VideoThumbs thumbs;
	std::vector<std::string> videoPaths;
	FolderList folders;
	LinkBridge link;
	Instrument instrument(params, source, folders, [&link](float bpm) { link.setTempo(bpm); });
	SetupSaver setupSaver(support / "setup.json", 0.2);
	{
		PerformanceSetup setup;
		if (readSetup(support / "setup.json", setup)) {
			applySetup(params, setup);
			RestoredFolders restored = restoreFolders(setup);
			for (const std::string& folder : restored.paths)
				folders.add({folder, scanVideos(folder)});
			if (restored.current.empty() || !instrument.chooseFolder(restored.current).ok) {
				params.setString("videos_folder", "");
				params.setInt("videos_count", 0);
			}
		}
		instrument.restoreSource();
		setupSaver.settle(captureSetup(params, folders));
	}
	float shownLoop = 0;
	float shownEnvelope = 0;
	int shownStage = 0;
	bool shownGate = false;
	bool shownCue = false;
	bool shownErase = false;
	std::string exportFailure;

	Clock clock;
	clock.setQuantum(params.getFloat("quantum"));
	AVFRecorder recorder;
	Export diskExport(params, instrument, recorder, compositor);
	OverdubGate gate;

	float readFps = 60.0f;
	double lastTime = glfwGetTime();

	Hotkeys hotkeys;

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		KeysDown down;
		HotkeyHolds levels;
		auto keyDown = [&](int key) { return glfwGetKey(window, key) == GLFW_PRESS; };
		down.showUi = keyDown(GLFW_KEY_U);
		const int digitKey[] = {GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
		                        GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9};
		for (int d = 0; d < 10; ++d)
			down.digit[d] = keyDown(digitKey[d]);
		down.sourcePrev = keyDown(GLFW_KEY_MINUS);
		down.sourceNext = keyDown(GLFW_KEY_EQUAL);
		down.blendPrev = keyDown(GLFW_KEY_LEFT_BRACKET);
		down.blendNext = keyDown(GLFW_KEY_RIGHT_BRACKET);
		down.fxPrev = keyDown(GLFW_KEY_APOSTROPHE);
		down.fxNext = keyDown(GLFW_KEY_BACKSLASH);
		down.randomVideo = keyDown(GLFW_KEY_V);
		levels.overdub = keyDown(GLFW_KEY_SPACE);
		levels.cue = keyDown(GLFW_KEY_C);
		HotkeyHolds hotkeyHolds = hotkeys.frame(instrument, down, levels, ImGui::GetIO().WantCaptureKeyboard);
		if (params.getBool("preview_window")) {
			if (!previewWindow) {
				previewWindow = glfwCreateWindow(640, 360, "Preview", nullptr, window);
				if (previewWindow) {
					glfwMakeContextCurrent(previewWindow);
					glfwSwapInterval(1);
					glfwMakeContextCurrent(window);
				}
			}
		} else {
			if (previewWindow) {
				glfwMakeContextCurrent(previewWindow);
				compositor.releaseSharedQuad();
				glfwDestroyWindow(previewWindow);
				previewWindow = nullptr;
				glfwMakeContextCurrent(window);
			}
		}

		int w, h;
		glfwGetFramebufferSize(window, &w, &h);
		glViewport(0, 0, w, h);

		double now = glfwGetTime();
		float t = static_cast<float>(now);
		float dt = static_cast<float>(now - lastTime);
		lastTime = now;

		reloadShaders(hot, compositor, fx, sourceFx);

		source.tick(dt);
		instrument.record(source.status());
		int srcKind = params.getInt("source_kind");
		// A Video or webcam picture at its own size, ready to be placed in the frame.
		bool hasPicture = (srcKind == (int)SourceKind::File || srcKind == (int)SourceKind::Webcam) &&
		                  source.frameWidth() > 0 && source.frameHeight() > 0;
		SourcePlacement placed{};
		if (hasPicture) placed = sourcePlacement(kLoopWidth, kLoopHeight, source.frameWidth(), source.frameHeight(), params);
		WholeStamp sourceStamp = compositor.drawSource(static_cast<SourceKind>(srcKind), t, placed,
		                                               params.getBool("video_bars"), hasPicture);

		float bpm = params.getFloat("bpm");
		float quantum = params.getFloat("quantum");
		clock.setQuantum(quantum);
		bool linkEnabled = params.getBool("link_enabled");
		link.setEnabled(linkEnabled);
		link.setQuantum(quantum);
		link.tick(dt);
		if (linkEnabled) {
			link.setStartStopSyncEnabled(params.getBool("link_startstop"));
			bool lp = params.getBool("link_playing");
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
		if (params.getBool("tempo_coupled"))
			readFps = 60.0f * static_cast<float>(bt.bpm / 120.0) * params.getFloat("read_speed_mult");
		else
			readFps = 60.0f * params.getFloat("read_speed_mult");

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, w, h);
		glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		const PreparedFolder* folder = folders.current();
		std::vector<std::string> videos = folder ? folder->videos : std::vector<std::string>{};
		if (videos != videoPaths) {
			videoPaths = std::move(videos);
			thumbs.setVideos(videoPaths);
		}
		std::vector<std::pair<std::string, std::string>> madeThumbs;
		WebLive live{params, folders};
		live.loop = shownLoop;
		live.envelope = shownEnvelope;
		live.stage = shownStage;
		live.gate = shownGate;
		live.cue = shownCue;
		live.erase = shownErase;
		live.thumbsFresh = thumbs.take(madeThumbs);
		live.thumbs = std::move(madeThumbs);
		live.loadFailure = source.takeLoadFailure();
		live.loadSwapped = source.loadSwapped();
		live.exportFailure = exportFailure;
		live.enabled = params.getBool("ipad_page");
		live.tokenRequired = params.getBool("ipad_token");
		live.silenceMs = int(params.getFloat("silence_ms"));
		WebInterface::Step step = web.frame(instrument, live);
		if (step.hasDraw) {
			std::vector<unsigned char> bgra;
			int drawW = 0;
			int drawH = 0;
			if (decodeDrawImage(step.draw, bgra, drawW, drawH)) compositor.uploadDraw(bgra, drawW, drawH);
		}

		GateHolds holds;
		holds.latch = params.getBool("arm_record");
		holds.space = hotkeyHolds.overdub;
		holds.pad = step.gate;
		holds.erase = step.erase;
		holds.cueLatch = params.getBool("cue_active");
		holds.cueKey = hotkeyHolds.cue;
		holds.cuePad = step.cue;
		GateFrame dub = gate.step(holds, gateSettings(params), source.committing(), dt);
		if (dub.restartVideo) instrument.restartVideo();

		GLuint liveSource = compositor.image(Picture::Source);
		if (params.getBool("source_fx_enabled") && liveSource != 0)
			liveSource = sourceFx.process(liveSource, liveSource, 0.0f, fxSettings(params, "source_fx_"), sourceStamp);

		GLuint dubImage = dub.image == DubImage::Black ? compositor.blackTexture() : liveSource;

		LoopWrite write;
		write.cue = dub.cue;
		write.overdub = dub.overdub;
		write.opacity = dub.opacity;
		write.blendMode = dub.blendMode;
		write.image = dubImage;
		write.bake = dub.image == DubImage::ProgramFx;
		write.replaceWith = write.bake ? fx.getPrevOutputTexture() : 0;
		LoopPicture looped = looper.step(dt, readFps, write, compositor);
		LoopRead loopRead{looped.tex0, looped.tex1, looped.mix * params.getFloat("gain_mix")};

		PictureMoment moment = PictureMoment::Idle;
		if (dub.overdub && dub.image == DubImage::ProgramFx)
			moment = PictureMoment::Bake;
		else if (dub.cue)
			moment = PictureMoment::Cue;
		else if (dub.overdub)
			moment = PictureMoment::Overdub;
		PictureChoice pictures = choosePictures(moment, dub.opacity, params.getFloat("record_blend_opacity"));

		GLuint fxOutput = 0;
		if (params.getBool("fx_enabled"))
			fxOutput = fx.process(loopRead.tex0, loopRead.tex1, loopRead.mix, fxSettings(params, "fx_"));
		compositor.program(loopRead, fxOutput);
		compositor.audition(loopRead, dubImage, pictures.previewOpacity, dub.blendMode);

		ContainRect program = containRect(kLoopWidth, kLoopHeight, w, h);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		if (program.w > 0 && program.h > 0)
			compositor.show(compositor.image(pictures.shown), program.x, program.y, program.w, program.h);

		diskExport.frame(dt, pictures.exported, kLoopWidth, kLoopHeight);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		exportFailure = diskExport.failure();

		if (params.getBool("show_preview") && !params.getBool("preview_window")) {
			float scale = params.getFloat("preview_scale");
			int margin = 12;
			ContainRect inset =
				containRect(kLoopWidth, kLoopHeight, static_cast<int>(w * scale), static_cast<int>(h * scale));
			GLuint preview = compositor.image(pictures.previewed);
			if (params.getBool("debug_preview_backbuffer")) preview = ring.textureAtBack(looped.idx0);
			if (inset.w > 0 && inset.h > 0)
				compositor.show(preview, w - inset.w - margin, margin, inset.w, inset.h);
			glViewport(0, 0, w, h);
		}

		if (previewWindow && !glfwWindowShouldClose(previewWindow)) {
			glfwMakeContextCurrent(previewWindow);
			int pw, ph;
			glfwGetFramebufferSize(previewWindow, &pw, &ph);
			glViewport(0, 0, pw, ph);
			glClearColor(0.07f, 0.07f, 0.08f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			ContainRect fitted = containRect(kLoopWidth, kLoopHeight, pw, ph);
			if (fitted.w > 0 && fitted.h > 0)
				compositor.show(compositor.image(pictures.previewed), fitted.x, fitted.y, fitted.w, fitted.h);
			glfwSwapBuffers(previewWindow);
			glfwMakeContextCurrent(window);
		} else if (previewWindow && glfwWindowShouldClose(previewWindow)) {
			glfwMakeContextCurrent(previewWindow);
			compositor.releaseSharedQuad();
			glfwDestroyWindow(previewWindow);
			previewWindow = nullptr;
			glfwMakeContextCurrent(window);
			params.setBool("preview_window", false);
		}

		shownLoop = (float(looped.idx0) + looped.mix) / float(ring.size());
		shownEnvelope = dub.envelope;
		shownStage = dub.stage;
		shownGate = dub.gate;
		shownCue = dub.cue;
		shownErase = dub.erase;

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		{
			ControlsPanel panel;
			panel.draw(params, instrument, link, ring, dt, source, web, folders);
		}
		if (params.getBool("show_playheads")) looper.drawHeadsOverlay(w, h);
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
	return 0;
}
