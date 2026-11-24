#include "App.hpp"

#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "control/Instrument.hpp"
#include "gl/Compositor.hpp"
#include "gl/FxPipeline.hpp"
#include "util/Repeat.hpp"
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
	FullscreenQuad* quadPreview = nullptr;

	int fbw, fbh;
	glfwGetFramebufferSize(window, &fbw, &fbh);
	RingBuffer ring;
	ring.setup(fbw, fbh, 120);
	LooperEngine looper(ring);
	looper.init(fbw, fbh);
	const std::string shaderDir = SHADER_DIR;
	Compositor compositor;
	compositor.init(fbw, fbh, shaderDir);
	SourceController source(compositor, avfDecoders());
	FxPipeline fx;
	FxPipeline sourceFx;
	fx.init(fbw, fbh, shaderDir);
	sourceFx.init(fbw, fbh, shaderDir);

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
		if (params.getInt("source_kind") == (int)SourceKind::Webcam) instrument.chooseWebcam();
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

	// Hotkey state (for edge-triggered actions)
	bool prevU = false;
	bool prevMinus = false, prevEqual = false;
	bool prevLBracket = false, prevRBracket = false;
	bool prevApostrophe = false, prevBackslash = false;
	bool prevV = false;
	bool prevNumber[10] = {};

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
				if (edgePressed(GLFW_KEY_U, prevU)) instrument.setSwitch("show_ui", !params.getBool("show_ui"));
				// Opacity on the number keys (1..9 => 0.1..0.9, 0 => 1.0)
				const int numberKeys[] = {GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5,
				                          GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9, GLFW_KEY_0};
				for (int k = 0; k < 10; ++k)
					if (edgePressed(numberKeys[k], prevNumber[k]))
						instrument.setNumber("record_blend_opacity", (k + 1) / 10.0);
				// Prev/Next Source kind (- / =)
				if (edgePressed(GLFW_KEY_MINUS, prevMinus)) {
					int s = params.getInt("source_kind");
					if (s == (int)SourceKind::Draw) instrument.setNumber("source_kind", (int)SourceKind::Black);
					else if (s > (int)SourceKind::Shader) instrument.setNumber("source_kind", s - 1);
				}
				if (edgePressed(GLFW_KEY_EQUAL, prevEqual)) {
					int s = params.getInt("source_kind");
					if (s < (int)SourceKind::Black) instrument.setNumber("source_kind", s + 1);
				}
				// Prev/Next blend mode ([ / ]) and Program FX mode (' / \); the instrument stops at either end
				if (edgePressed(GLFW_KEY_LEFT_BRACKET, prevLBracket))
					instrument.setNumber("record_blend_mode", params.getInt("record_blend_mode") - 1);
				if (edgePressed(GLFW_KEY_RIGHT_BRACKET, prevRBracket))
					instrument.setNumber("record_blend_mode", params.getInt("record_blend_mode") + 1);
				if (edgePressed(GLFW_KEY_APOSTROPHE, prevApostrophe))
					instrument.setNumber("fx_mode", params.getInt("fx_mode") - 1);
				if (edgePressed(GLFW_KEY_BACKSLASH, prevBackslash))
					instrument.setNumber("fx_mode", params.getInt("fx_mode") + 1);
				if (edgePressed(GLFW_KEY_V, prevV)) instrument.randomVideo();
			}
		}
		if (params.getBool("preview_window")) {
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
			fx.resize(fbw, fbh);
			sourceFx.resize(fbw, fbh);
			looper.resize(fbw, fbh);
			compositor.resize(fbw, fbh);
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

		compositor.hotReload(hot);
		sourceFx.applyReload(fx.hotReload(hot));

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

		source.tick(dt, params);
		std::string drawPng;
		if (remote.takeDraw(drawPng)) {
			std::vector<unsigned char> bgra;
			int drawW = 0;
			int drawH = 0;
			if (decodeDrawImage(drawPng, bgra, drawW, drawH)) compositor.uploadDraw(bgra, drawW, drawH);
		}
		int srcKind = params.getInt("source_kind");
		// A Video or webcam picture at its own size, ready to be placed in the frame.
		bool picture = (srcKind == (int)SourceKind::File || srcKind == (int)SourceKind::Webcam) &&
		               source.frameWidth() > 0 && source.frameHeight() > 0;
		SourcePlacement placed{};
		if (picture) placed = sourcePlacement(fbw, fbh, source.frameWidth(), source.frameHeight(), params);
		if (srcKind == (int)SourceKind::Shader)
			compositor.drawTestPattern(t);
		else if (srcKind == (int)SourceKind::Black)
			compositor.drawBlack();
		else if (srcKind == (int)SourceKind::Draw)
			compositor.drawDraw();
		else if (picture)
			compositor.drawPlacedPicture(placed, params.getBool("video_bars"));

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

		ReadState rs = looper.advanceRead(dt, readFps);
		LoopRead loopRead{ring.textureAt(rs.idx0), ring.textureAt(rs.idx1), rs.mix * params.getFloat("gain_mix")};

		remote.setTokenRequired(params.getBool("ipad_token"));
		remote.setEnabled(params.getBool("ipad_page"));
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
		holds.space = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
		holds.pad = pads.gate;
		holds.erase = pads.erase;
		holds.cueLatch = params.getBool("cue_active");
		holds.cueKey = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
		holds.cuePad = pads.cue;
		GateFrame dub = gate.step(holds, gateSettings(params), source.committing(), dt);
		if (dub.restartVideo && params.getInt("file_run_mode") == 1 &&
		    params.getInt("source_kind") == (int)SourceKind::File)
			source.restartFileAtLoopStart();

		GLuint liveSource = compositor.sourceTexture();
		if (params.getBool("source_fx_enabled") && liveSource != 0) {
			FxSettings settings = fxSettings(params, "source_fx_");
			// Repeat and Grid stamp the whole picture, so one that runs past the frame is not cropped first.
			if (fxStampsCopies(settings.mode) && picture)
				sourceFx.setWholeStamp(compositor.pictureTexture(), placed.centerX, placed.centerY, placed.halfX,
				                       placed.halfY, placed.rot);
			else
				sourceFx.setWholeStamp(0, 0, 0, 0, 0, 0);
			liveSource = sourceFx.process(liveSource, liveSource, 0.0f, settings, compositor.quad());
		}

		GLuint dubImage = dub.image == DubImage::Black ? compositor.blackTexture() : liveSource;

		// Cue shows the Audition and leaves the Loop alone.
		if (!dub.cue) {
			LoopWrite write;
			write.overdub = dub.overdub;
			write.opacity = dub.opacity;
			write.blendMode = dub.blendMode;
			write.image = dubImage;
			write.replaceWith = dub.image == DubImage::ProgramFx ? fx.getPrevOutputTexture() : 0;
			looper.writeAndSwap(compositor.blitShader(), compositor.blendShader(), compositor.quad(), rs.idx0, rs.wraps,
			                    write);
		}

		// The window shows the Audition while cueing or overdubbing, and the Program otherwise.
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		if (dub.cue || dub.overdub) {
			compositor.show(compositor.audition(loopRead, dubImage, dub.opacity, dub.blendMode), 0, 0, w, h);
		} else if (params.getBool("fx_enabled")) {
			GLuint fxOut =
				fx.process(loopRead.tex0, loopRead.tex1, loopRead.mix, fxSettings(params, "fx_"), compositor.quad());
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
				params.getBool("debug_preview_backbuffer") ? ring.textureAtBack(rs.idx0) : compositor.auditionTexture();
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
			compositor.blitShader().use();
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, compositor.auditionTexture());
			compositor.blitShader().setUniform("uTex", 0);
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
			RemoteLive live{
				(float(rs.idx0) + rs.mix) / float(ring.size()), dub.envelope, dub.stage, dub.gate, dub.cue, dub.erase};
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
