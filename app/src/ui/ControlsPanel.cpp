#include "ui/ControlsPanel.hpp"

#include "control/ControlTable.hpp"
#include "imgui.h"
#include "remote/QrCode.hpp"
#include "util/MacFileDialog.hpp"
#include "util/Repeat.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>

void ControlsPanel::draw(const ParamBus& params, Instrument& instrument, const LinkBridge& link, const RingBuffer& ring,
                         float dt, const SourceController& source, const RemoteSession& remote,
                         const FolderList& folders) {
	if (!params.getBool("show_ui")) return;
	if (!ImGui::Begin("Controls")) {
		ImGui::End();
		return;
	}

	// One widget per control. Ranges come from the control table.
	auto toggle = [&](const char* label, const char* name) {
		bool on = params.getBool(name);
		if (ImGui::Checkbox(label, &on)) instrument.setSwitch(name, on);
		return on;
	};
	auto slider = [&](const char* label, const char* name, const char* format = "%.3f") {
		const Control* control = findControl(name);
		float v = params.getFloat(name);
		if (ImGui::SliderFloat(label, &v, control->min, control->max, format)) instrument.setNumber(name, v);
	};
	auto choice = [&](const char* label, const char* name, const char* const* items, int count) {
		int v = params.getInt(name);
		if (ImGui::Combo(label, &v, items, count)) instrument.setNumber(name, v);
		return v;
	};
	auto textField = [&](const char* label, const char* name) {
		char buf[512];
		std::string current = params.getString(name);
		if (current.size() >= sizeof(buf)) current.clear();
		std::strncpy(buf, current.c_str(), sizeof(buf));
		buf[sizeof(buf) - 1] = '\0';
		if (ImGui::InputText(label, buf, sizeof(buf))) instrument.setText(name, std::string(buf));
	};
	auto heading = [](const char* title) {
		ImGui::Separator();
		ImGui::TextUnformatted(title);
		ImGui::Separator();
	};
	// The async dialogs answer after this frame, so their callbacks hold the instrument and nothing local.
	Instrument* inst = &instrument;

	if (ImGui::BeginTabBar("ControlsTabs")) {
		if (ImGui::BeginTabItem("Live")) {
			ImGui::TextUnformatted("Recording");
			ImGui::Separator();
			toggle("Enable Recording", "record_enabled");
			textField("Output Path", "record_path");
			if (ImGui::Button("Save As...")) {
				std::string picked = macSaveFileDialog("Save Recording", "recording.mov");
				if (!picked.empty()) instrument.setText("record_path", picked);
			}
			slider("Record FPS", "record_fps");

			ImGui::TextUnformatted("Record & Cue");
			ImGui::Separator();
			toggle("Arm Record", "arm_record");
			toggle("Cue", "cue_active");

			heading("Source");
			const char* kinds[] = {"Shader", "File", "Black", "Webcam", "Draw"};
			int kind = choice("Source", "source_kind", kinds, 5);
			if (kind != (int)SourceKind::File) filePathActive = false;
			int camIndex = params.getInt("webcam_index");
			if (ImGui::InputInt("Webcam Index", &camIndex)) instrument.setNumber("webcam_index", camIndex);
			toggle("Black bars", "video_bars");
			toggle("Preserve aspect", "video_aspect");
			if (kind == (int)SourceKind::File) {
				if (!filePathActive) {
					filePathBuf = params.getString("file_path");
					if (filePathBuf.size() >= 512) filePathBuf.clear();
				}
				char filePath[512];
				std::strncpy(filePath, filePathBuf.c_str(), sizeof filePath);
				filePath[sizeof filePath - 1] = '\0';
				ImGui::InputText("File Path", filePath, sizeof filePath);
				filePathBuf = filePath;
				if (ImGui::IsItemDeactivatedAfterEdit()) instrument.setText("file_path", filePathBuf);
				filePathActive = ImGui::IsItemActive();
				if (ImGui::Button("Browse...")) {
					macOpenFileDialogAsync("Select Video File", [inst](std::string p) {
						if (!p.empty()) inst->loadVideo(p);
					});
				}
				textField("Videos Folder", "videos_folder");
				if (ImGui::Button("Choose Folder")) {
					macOpenDirectoryDialogAsync("Select Videos Folder", [inst](std::string fp) {
						if (!fp.empty()) inst->setText("videos_folder", fp);
					});
				}
				ImGui::Text("Folder Clips: %d", params.getInt("videos_count"));
				if (source.committing() && !source.commitVideo().empty())
					ImGui::Text("Committing: %s", source.commitVideo().c_str());
				if (ImGui::Button("Load Random File")) instrument.randomVideo();
				if (ImGui::Button("Prev File")) instrument.prevVideo();
				if (ImGui::Button("Next File")) instrument.nextVideo();
				toggle("Loop File", "file_loop");
				toggle("Pause File", "file_paused");
				ImGui::Text("Source FPS: %.2f", source.getFileNominalFps());

				const char* runModes[] = {"Free", "Trigger"};
				choice("File Run Mode", "file_run_mode", runModes, 2);

				// Loop range, as fractions of the Video
				slider("Loop Start", "file_loop_start");
				slider("Loop Length", "file_loop_length");
			}

			heading("Folder list");
			for (const std::string& p : folders.paths()) {
				std::string label = std::filesystem::path(p).filename().string();
				if (label.empty()) label = p;
				label += "###" + p;
				bool chosen = p == params.getString("videos_folder");
				if (ImGui::Selectable(label.c_str(), chosen)) instrument.chooseFolder(p);
			}
			if (ImGui::Button("Add Folder")) {
				macOpenDirectoryDialogAsync("Add Videos Folder", [inst](std::string fp) {
					if (!fp.empty()) inst->addFolder(fp, scanVideos(fp));
				});
			}

			heading("Blending");
			slider("Blend Gain", "gain_mix");
			const char* blendModes[] = {"Mix",     "Add",    "Multiply",   "Screen",     "Overlay",     "Difference",
			                            "Lighten", "Darken", "Hard Light", "Soft Light", "Color Dodge", "Color Burn"};
			choice("Record Blend Mode", "record_blend_mode", blendModes, 12);
			slider("Record Opacity", "record_blend_opacity");

			heading("Envelope (ADSR)");
			toggle("Use Envelope (ADSR)", "env_enabled");
			slider("Attack (s)", "env_attack");
			slider("Decay (s)", "env_decay");
			slider("Sustain", "env_sustain");
			slider("Release (s)", "env_release");

			heading("FX");
			const char* fxModes[] = {"Feedback", "Difference", "Chromatic", "Kaleido", "Blur", "Repeat", "Grid"};
			// prefix is "fx_" for Program FX and "source_fx_" for Source FX.
			auto drawFxKnobs = [&](const std::string& prefix, int mode) {
				std::string k1 = prefix + "param1", k2 = prefix + "param2", k3 = prefix + "param3";
				std::string kCount = prefix + "count", kBlend = prefix + "repeat_blend";
				ImGui::PushID(prefix.c_str());
				if (fxStampsCopies(mode)) {
					float dist = params.getFloat(k1) * 100.0f;
					if (ImGui::SliderFloat("Distance", &dist, 0.0f, 100.0f, "%.0f%%"))
						instrument.setNumber(k1, dist / 100.0f);
					float deg = params.getFloat(k2) * 360.0f;
					if (ImGui::SliderFloat("Angle", &deg, 0.0f, 360.0f, "%.0f deg"))
						instrument.setNumber(k2, deg / 360.0f);
					int copies = params.getInt(kCount);
					int maxCopies = mode == kGridFxMode ? kGridMaxCopies : kRepeatMaxCopies;
					int shown = std::min(copies, maxCopies);
					if (ImGui::SliderInt("Copies", &shown, 0, maxCopies) && shown != std::min(copies, maxCopies))
						instrument.setNumber(kCount, shown);
					choice("Blend", kBlend.c_str(), blendModes, 12);
				} else {
					if (mode == 3) {
						float segments = params.getFloat(k1) * 12.0f + 1.0f;
						if (ImGui::SliderFloat("Segments", &segments, 1.0f, 13.0f, "%.0f"))
							instrument.setNumber(k1, (segments - 1.0f) / 12.0f);
					} else if (mode != 1) {
						slider(mode == 2 ? "Chroma" : mode == 4 ? "Radius" : "Mix", k1.c_str());
					}
					slider(mode == 1 ? "Scale" : "Decay", k2.c_str());
					slider(mode == 1 ? "Decay" : "Zoom", k3.c_str());
				}
				ImGui::PopID();
			};
			toggle("FX Enabled", "fx_enabled");
			toggle("Commit FX to Loop", "fx_commit");
			drawFxKnobs("fx_", choice("FX Mode", "fx_mode", fxModes, 7));

			heading("Source FX");
			toggle("Source FX", "source_fx_enabled");
			drawFxKnobs("source_fx_", choice("Source FX Mode", "source_fx_mode", fxModes, 7));

			heading("Web interface");
			bool ipad = toggle("Web interface", "ipad_page");
			toggle("URL token", "ipad_token");
			slider("Silence window (ms)", "silence_ms");
			if (ipad && remote.enabled()) {
				std::string url = remote.url();
				ImGui::InputText("URL", url.data(), url.size() + 1, ImGuiInputTextFlags_ReadOnly);
				static std::string qrUrl;
				static std::vector<std::vector<bool>> qr;
				if (url != qrUrl) {
					qrUrl = url;
					qr = qrEncode(url);
				}
				if (!qr.empty()) {
					const float cell = 4.0f;
					const float quiet = 4.0f;
					const float side = cell * (float(qr.size()) + 2.0f * quiet);
					ImVec2 origin = ImGui::GetCursorScreenPos();
					ImDrawList* draw = ImGui::GetWindowDrawList();
					draw->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + side), IM_COL32_WHITE);
					for (size_t y = 0; y < qr.size(); ++y) {
						for (size_t x = 0; x < qr.size(); ++x) {
							if (!qr[y][x]) continue;
							ImVec2 p(origin.x + (float(x) + quiet) * cell, origin.y + (float(y) + quiet) * cell);
							draw->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), IM_COL32_BLACK);
						}
					}
					ImGui::Dummy(ImVec2(side, side));
				}
				ImGui::TextUnformatted(remote.clientCount() > 0 ? "iPad connected" : "iPad not connected");
			}

			ImGui::Separator();
			ImGui::Text("FPS: %.1f", 1.0f / std::max(1e-6f, dt));

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Settings")) {
			ImGui::TextUnformatted("UI");
			ImGui::Separator();
			toggle("Show UI", "show_ui");
			toggle("Separate Preview Window", "preview_window");
			slider("Preview Scale", "preview_scale");
			toggle("Show Preview", "show_preview");
			toggle("Show Playheads", "show_playheads");
			toggle("Preview Backbuffer", "debug_preview_backbuffer");

			heading("Link");
			toggle("Ableton Link", "link_enabled");
			ImGui::Text("Link Peers: %d", link.getNumPeers());
			toggle("Link Start/Stop Sync", "link_startstop");
			toggle("Link Playing", "link_playing");

			heading("Tempo & Speed");
			slider("BPM", "bpm");
			slider("Quantum", "quantum");
			toggle("Tempo Coupled Read", "tempo_coupled");
			slider("Read Speed Mult", "read_speed_mult");

			heading("Debug");
			ImGui::Text("Current Read Buffer: %c", ring.getReadBank() == 0 ? 'A' : 'B');
			ImGui::Text("Current Write Buffer: %c", ring.getWriteBank() == 0 ? 'A' : 'B');
			ImGui::Separator();
			ImGui::Text("Ableton Link SDK: %s", link.isAvailable() ? "Loaded" : "Not compiled in");

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
}
