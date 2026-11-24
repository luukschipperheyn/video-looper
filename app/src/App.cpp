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
#include "buffer/RingBuffer.hpp"
#include "looper/LooperEngine.hpp"
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
#include "remote/RemoteApply.hpp"
#include "remote/RemoteSession.hpp"
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

	int fbw, fbh;
	glfwGetFramebufferSize(window, &fbw, &fbh);
	RingBuffer ring;
	LooperEngine looper(ring);
	const std::string shaderDir = SHADER_DIR;
	Compositor compositor;
	FxPipeline fx;
	FxPipeline sourceFx;
	initFrame(fbw, fbh, shaderDir, ring, looper, compositor, fx, sourceFx);
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
	RemoteSession remote(support / "remote-token");
	remote.setUsageFile(support / "web-use.jsonl");
	std::string remoteSnapshotSent;
	VideoThumbs thumbs;
	std::vector<std::string> videoPaths;
	std::vector<std::string> videoNames;
	std::vector<std::string> sentVideoNames;
	std::vector<std::string> sentFolderPaths;
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
	std::string sourceFailure;

	Clock clock;
	clock.setQuantum(params.getFloat("quantum"));
	AVFRecorder recorder;
	double recordAccumSeconds = 0.0;
	double recordFramePeriod = 1.0 / 60.0;
	bool prevRecEnabled = false;
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
		for (int d = 0; d < 10; ++d) down.digit[d] = keyDown(digitKey[d]);
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
		if (w != fbw || h != fbh) {
			fbw = w;
			fbh = h;
			resizeFrame(fbw, fbh, ring, looper, compositor, fx, sourceFx);
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

		reloadShaders(hot, compositor, fx, sourceFx);

		// Manage recording state edge transitions
		bool recEnabled = params.getBool("record_enabled");
		if (recEnabled && !prevRecEnabled) {
			std::string outPath = params.getString("record_path");
			double rfps = params.getFloat("record_fps");
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

		source.tick(dt);
		instrument.record(source.status());
		std::string drawPng;
		if (remote.takeDraw(drawPng)) {
			std::vector<unsigned char> bgra;
			int drawW = 0;
			int drawH = 0;
			if (decodeDrawImage(drawPng, bgra, drawW, drawH)) compositor.uploadDraw(bgra, drawW, drawH);
		}
		int srcKind = params.getInt("source_kind");
		// A Video or webcam picture at its own size, ready to be placed in the frame.
		bool hasPicture = (srcKind == (int)SourceKind::File || srcKind == (int)SourceKind::Webcam) &&
		                  source.frameWidth() > 0 && source.frameHeight() > 0;
		SourcePlacement placed{};
		if (hasPicture) placed = sourcePlacement(fbw, fbh, source.frameWidth(), source.frameHeight(), params);
		WholeStamp sourceStamp =
			compositor.drawSource(static_cast<SourceKind>(srcKind), t, placed, params.getBool("video_bars"), hasPicture);

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

		remote.setTokenRequired(params.getBool("ipad_token"));
		remote.setEnabled(params.getBool("ipad_page"));
		const PreparedFolder* folder = folders.current();
		std::vector<std::string> videos = folder ? folder->videos : std::vector<std::string>{};
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
			remote.setSilenceWindow(int(params.getFloat("silence_ms")));
			if (!loadFailure.empty()) remote.send(rejectFrame("source", loadFailure));
			for (const RemoteSet& set : remote.drain()) {
				std::string reject = applyRemote(instrument, set);
				if (!reject.empty()) remote.send(reject);
			}
		} else {
			params.setBool("ipad_page", false);
		}
		RemoteSession::PadState pads = remote.pads();

		GateHolds holds;
		holds.latch = params.getBool("arm_record");
		holds.space = hotkeyHolds.overdub;
		holds.pad = pads.gate;
		holds.erase = pads.erase;
		holds.cueLatch = params.getBool("cue_active");
		holds.cueKey = hotkeyHolds.cue;
		holds.cuePad = pads.cue;
		GateFrame dub = gate.step(holds, gateSettings(params), source.committing(), dt);
		if (dub.restartVideo) instrument.restartVideo();

		GLuint liveSource = compositor.sourceTexture();
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

		// The window shows the Audition while cueing or overdubbing, and the Program otherwise.
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		if (dub.cue || dub.overdub) {
			compositor.show(compositor.audition(loopRead, dubImage, dub.opacity, dub.blendMode), 0, 0, w, h);
		} else if (params.getBool("fx_enabled")) {
			GLuint fxOut = fx.process(loopRead.tex0, loopRead.tex1, loopRead.mix, fxSettings(params, "fx_"));
			compositor.show(fxOut, 0, 0, w, h);
		} else {
			compositor.showLoop(loopRead, 0, 0, w, h);
		}

		// The preview and Export show what an Overdub would produce at full envelope.
		bool previewVisible = params.getBool("show_preview") || params.getBool("preview_window");
		bool idle = !dub.cue && !dub.overdub;
		if ((previewVisible && idle) || params.getBool("record_enabled"))
			compositor.audition(loopRead, liveSource, params.getFloat("record_blend_opacity"),
			                    params.getInt("record_blend_mode"));

		// Capture the Audition into the recorder at the export frame rate
		if (recorder.isRecording()) {
			recordAccumSeconds += dt;
			while (recordAccumSeconds + 1e-6 >= recordFramePeriod) {
				recordAccumSeconds -= recordFramePeriod;
				std::printf("[rec] capture frame begin %dx%d\n", fbw, fbh);
				const std::vector<unsigned char>& frame = compositor.readAudition();
				std::printf("[rec] append start\n");
				bool ok = recorder.appendBGRAFrame(frame.data(), fbw * 4);
				std::printf("[rec] append %s\n", ok ? "ok" : "fail");
				if (!ok) {
					std::fprintf(stderr, "[rec] append dropped (not ready)\n");
					break;
				}
			}
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}

		if (params.getBool("show_preview") && !params.getBool("preview_window")) {
			float scale = params.getFloat("preview_scale");
			int pw = static_cast<int>(w * scale);
			int ph = static_cast<int>(pw * (static_cast<float>(fbh) / static_cast<float>(fbw)));
			int margin = 12;
			GLuint shown =
				params.getBool("debug_preview_backbuffer") ? ring.textureAtBack(looped.idx0) : compositor.auditionTexture();
			compositor.show(shown, w - pw - margin, margin, pw, ph);
			glViewport(0, 0, w, h);
		}

		if (previewWindow && !glfwWindowShouldClose(previewWindow)) {
			glfwMakeContextCurrent(previewWindow);
			int pw, ph;
			glfwGetFramebufferSize(previewWindow, &pw, &ph);
			glViewport(0, 0, pw, ph);
			glClearColor(0.07f, 0.07f, 0.08f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			compositor.show(compositor.auditionTexture(), 0, 0, pw, ph);
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

		if (remote.enabled()) {
			RemoteLive live{
				(float(looped.idx0) + looped.mix) / float(ring.size()), dub.envelope, dub.stage, dub.gate, dub.cue,
				dub.erase};
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
			panel.draw(params, instrument, link, ring, dt, source, remote, folders);
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
