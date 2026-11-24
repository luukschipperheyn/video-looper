#include "ui/ControlsPanel.hpp"

#include "imgui.h"
#include "util/MacFileDialog.hpp"
#include "util/Repeat.hpp"
#include "remote/QrCode.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>

void ControlsPanel::draw(ParamBus& params, LinkBridge& link, RingBuffer& ring, float dt, float& outBpm,
                         float& outQuantum, SourceController* source, const RemoteSession& remote,
                         FolderList& folders) {
	if (!params.getBool("show_ui", true)) return;
	if (!ImGui::Begin("Controls")) {
		ImGui::End();
		return;
	}

	if (ImGui::BeginTabBar("ControlsTabs")) {
		if (ImGui::BeginTabItem("Live")) {
			ImGui::TextUnformatted("Recording");
			ImGui::Separator();
			{
				bool rec = params.getBool("record_enabled", false);
				if (ImGui::Checkbox("Enable Recording", &rec)) params.setBool("record_enabled", rec);
				static char rpath[512];
				std::string curp = params.getString("record_path", "");
				if (curp.size() >= sizeof(rpath)) curp.clear();
				std::strncpy(rpath, curp.c_str(), sizeof(rpath));
				rpath[sizeof(rpath) - 1] = '\0';
				if (ImGui::InputText("Output Path", rpath, sizeof(rpath))) {
					params.setString("record_path", std::string(rpath));
				}
				if (ImGui::Button("Save As...")) {
					std::string picked = macSaveFileDialog("Save Recording", "recording.mov");
					if (!picked.empty()) params.setString("record_path", picked);
				}
				float rfps = params.getFloat("record_fps", 60.0f);
				if (ImGui::SliderFloat("Record FPS", &rfps, 1.0f, 240.0f)) params.setFloat("record_fps", rfps);
			}

			ImGui::TextUnformatted("Record & Cue");
			ImGui::Separator();
			bool arm = params.getBool("arm_record", false);
			if (ImGui::Checkbox("Arm Record", &arm)) params.setBool("arm_record", arm);
			bool cue = params.getBool("cue_active", false);
			if (ImGui::Checkbox("Cue", &cue)) params.setBool("cue_active", cue);

			ImGui::Separator();
			ImGui::TextUnformatted("Source");
			ImGui::Separator();
			int kind = params.getInt("source_kind", (int)SourceKind::Shader);
			const char* kinds[] = {"Shader", "File", "Black", "Webcam", "Draw"};
			int camIndex = params.getInt("webcam_index", 0);
			if (ImGui::Combo("Source", &kind, kinds, 5)) {
				if (kind == (int)SourceKind::Webcam && source)
					source->loadWebcam(camIndex);
				else
					params.setInt("source_kind", kind);
			}
			if (ImGui::InputInt("Webcam Index", &camIndex)) params.setInt("webcam_index", camIndex);
			bool bars = params.getBool("video_bars", true);
			if (ImGui::Checkbox("Black bars", &bars)) params.setBool("video_bars", bars);
			bool aspect = params.getBool("video_aspect", true);
			if (ImGui::Checkbox("Preserve aspect", &aspect)) {
				params.setBool("video_aspect", aspect);
				if (aspect) params.setFloat("video_sy", params.getFloat("video_sx", 1.0f));
			}
			if (kind == (int)SourceKind::File) {
				static char buf[512];
				std::string cur = params.getString("file_path", "");
				if (cur.size() >= sizeof(buf)) cur.clear();
				std::strncpy(buf, cur.c_str(), sizeof(buf));
				buf[sizeof(buf) - 1] = '\0';
				if (ImGui::InputText("File Path", buf, sizeof(buf))) {
					params.setString("file_path", std::string(buf));
				}
				if (ImGui::Button("Browse...")) {
					macOpenFileDialogAsync("Select Video File", [source, &params](std::string p) {
						if (!p.empty() && source)
							source->loadVideo(p, params.getFloat("file_loop_start", 0.0f),
							                  params.getFloat("file_loop_length", 1.0f));
					});
				}
				static char fbuf[512];
				std::string fcur = params.getString("videos_folder", "");
				if (fcur.size() >= sizeof(fbuf)) fcur.clear();
				std::strncpy(fbuf, fcur.c_str(), sizeof(fbuf));
				fbuf[sizeof(fbuf) - 1] = '\0';
				if (ImGui::InputText("Videos Folder", fbuf, sizeof(fbuf))) {
					params.setString("videos_folder", std::string(fbuf));
				}
				// ImGui::SameLine();
				if (ImGui::Button("Choose Folder")) {
					macOpenDirectoryDialogAsync("Select Videos Folder", [&](std::string fp) {
						if (!fp.empty()) {
							params.setString("videos_folder", fp);
						}
					});
				}
				ImGui::Text("Folder Clips: %d", params.getInt("videos_count", 0));
				if (params.getBool("commit_active", false)) {
					std::string curf = params.getString("commit_current_file", "");
					if (!curf.empty()) {
						ImGui::Text("Committing: %s", curf.c_str());
					}
				}
				// ImGui::SameLine();
				if (ImGui::Button("Load Random File")) {
					if (source) source->triggerLoadRandomFile(params);
				}
				if (ImGui::Button("Prev File")) {
					if (source) source->loadPrevFile(params);
				}
				if (ImGui::Button("Next File")) {
					if (source) source->loadNextFile(params);
				}
				bool loop = params.getBool("file_loop", true);
				if (ImGui::Checkbox("Loop File", &loop)) params.setBool("file_loop", loop);
				bool paused = params.getBool("file_paused", false);
				if (ImGui::Checkbox("Pause File", &paused)) params.setBool("file_paused", paused);
				if (source) ImGui::Text("Source FPS: %.2f", source->getFileNominalFps());

				// Run Mode: 0 = Free, 1 = Trigger
				int runMode = params.getInt("file_run_mode", 0);
				const char* runModes[] = {"Free", "Trigger"};
				if (ImGui::Combo("File Run Mode", &runMode, runModes, 2)) params.setInt("file_run_mode", runMode);

				// Loop range controls: Start and Length (normalized 0..1)
				float ls = params.getFloat("file_loop_start", 0.0f);
				float ll = params.getFloat("file_loop_length", 1.0f);
				if (ImGui::SliderFloat("Loop Start", &ls, 0.0f, 1.0f)) {
					// Ensure start is not exactly at the end to allow a minimal range
					ls = std::clamp(ls, 0.0f, 0.999f);
					params.setFloat("file_loop_start", ls);
				}
				if (ImGui::SliderFloat("Loop Length", &ll, 0.0f, 1.0f)) {
					ll = std::clamp(ll, 0.0f, 1.0f);
					params.setFloat("file_loop_length", ll);
				}
			}

			ImGui::Separator();
			ImGui::TextUnformatted("Folder list");
			ImGui::Separator();
			for (const std::string& p : folders.paths()) {
				std::string label = std::filesystem::path(p).filename().string();
				if (label.empty()) label = p;
				label += "###" + p;
				bool chosen = p == params.getString("videos_folder", "");
				if (ImGui::Selectable(label.c_str(), chosen) && source && folders.select(p))
					source->showFolder(p, folders.current()->videos, params);
			}
			if (ImGui::Button("Add Folder")) {
				macOpenDirectoryDialogAsync("Add Videos Folder", [source, &folders, &params](std::string fp) {
					if (fp.empty() || !source) return;
					folders.add({fp, scanVideos(fp)});
					folders.select(fp);
					source->showFolder(fp, folders.current()->videos, params);
				});
			}

			ImGui::Separator();
			ImGui::TextUnformatted("Blending");
			ImGui::Separator();
			float gain = params.getFloat("gain_mix", 1.0f);
			if (ImGui::SliderFloat("Blend Gain", &gain, 0.0f, 1.0f)) params.setFloat("gain_mix", gain);
			int rbm = params.getInt("record_blend_mode", 0);
			const char* blendModes[] = {"Mix",     "Add",    "Multiply",   "Screen",     "Overlay",     "Difference",
			                            "Lighten", "Darken", "Hard Light", "Soft Light", "Color Dodge", "Color Burn"};
			if (ImGui::Combo("Record Blend Mode", &rbm, blendModes, 12)) params.setInt("record_blend_mode", rbm);
			float rop = params.getFloat("record_blend_opacity", 1.0f);
			if (ImGui::SliderFloat("Record Opacity", &rop, 0.0f, 1.0f)) params.setFloat("record_blend_opacity", rop);

			ImGui::Separator();
			ImGui::TextUnformatted("Envelope (ADSR)");
			ImGui::Separator();
			bool envEn = params.getBool("env_enabled", true);
			if (ImGui::Checkbox("Use Envelope (ADSR)", &envEn)) params.setBool("env_enabled", envEn);
			float att = params.getFloat("env_attack", 0.05f);
			if (ImGui::SliderFloat("Attack (s)", &att, 0.0f, 5.0f)) params.setFloat("env_attack", att);
			float dec = params.getFloat("env_decay", 0.20f);
			if (ImGui::SliderFloat("Decay (s)", &dec, 0.0f, 5.0f)) params.setFloat("env_decay", dec);
			float sus = params.getFloat("env_sustain", 1.0f);
			if (ImGui::SliderFloat("Sustain", &sus, 0.0f, 1.0f)) params.setFloat("env_sustain", sus);
			float rel = params.getFloat("env_release", 0.30f);
			if (ImGui::SliderFloat("Release (s)", &rel, 0.0f, 5.0f)) params.setFloat("env_release", rel);

			ImGui::Separator();
			ImGui::TextUnformatted("FX");
			ImGui::Separator();
			const char* fxModes[] = {"Feedback", "Difference", "Chromatic", "Kaleido", "Blur", "Repeat", "Grid"};
			auto drawFxKnobs = [&](const char* id, int mode, const char* k1, const char* k2, const char* k3,
			                       const char* kCount, const char* kBlend, float d1, float d2, float d3, int dCount) {
				ImGui::PushID(id);
				if (fxStampsCopies(mode)) {
					float dist = params.getFloat(k1, d1) * 100.0f;
					if (ImGui::SliderFloat("Distance", &dist, 0.0f, 100.0f, "%.0f%%"))
						params.setFloat(k1, dist / 100.0f);
					float deg = params.getFloat(k2, d2) * 360.0f;
					if (ImGui::SliderFloat("Angle", &deg, 0.0f, 360.0f, "%.0f deg")) params.setFloat(k2, deg / 360.0f);
					int copies = params.getInt(kCount, dCount);
					int maxCopies = mode == kGridFxMode ? kGridMaxCopies : kRepeatMaxCopies;
					int shown = std::min(copies, maxCopies);
					if (ImGui::SliderInt("Copies", &shown, 0, maxCopies) && shown != std::min(copies, maxCopies))
						params.setInt(kCount, shown);
					int blend = params.getInt(kBlend, 6);
					if (ImGui::Combo("Blend", &blend, blendModes, 12)) params.setInt(kBlend, blend);
				} else {
					float p1 = params.getFloat(k1, d1);
					float p2 = params.getFloat(k2, d2);
					float p3 = params.getFloat(k3, d3);
					if (mode == 3) {
						float segments = p1 * 12.0f + 1.0f;
						if (ImGui::SliderFloat("Segments", &segments, 1.0f, 13.0f, "%.0f"))
							params.setFloat(k1, (segments - 1.0f) / 12.0f);
					} else if (mode != 1) {
						const char* first = mode == 2 ? "Chroma" : mode == 4 ? "Radius" : "Mix";
						if (ImGui::SliderFloat(first, &p1, 0.0f, 1.0f)) params.setFloat(k1, p1);
					}
					const char* second = mode == 1 ? "Scale" : "Decay";
					if (ImGui::SliderFloat(second, &p2, 0.0f, 1.0f)) params.setFloat(k2, p2);
					const char* third = mode == 1 ? "Decay" : "Zoom";
					if (ImGui::SliderFloat(third, &p3, -0.05f, 0.05f)) params.setFloat(k3, p3);
				}
				ImGui::PopID();
			};
			bool fxEn = params.getBool("fx_enabled", false);
			if (ImGui::Checkbox("FX Enabled", &fxEn)) params.setBool("fx_enabled", fxEn);
			bool fxC = params.getBool("fx_commit", false);
			if (ImGui::Checkbox("Commit FX to Loop", &fxC)) params.setBool("fx_commit", fxC);
			int fxMode = params.getInt("fx_mode", 0);
			if (ImGui::Combo("FX Mode", &fxMode, fxModes, 7)) params.setInt("fx_mode", fxMode);
			drawFxKnobs("program", fxMode, "fx_param1", "fx_param2", "fx_param3", "fx_count", "fx_repeat_blend", 0.8f,
			            0.985f, 0.0f, 4);

			ImGui::Separator();
			ImGui::TextUnformatted("Source FX");
			ImGui::Separator();
			bool srcFx = params.getBool("source_fx_enabled", false);
			if (ImGui::Checkbox("Source FX", &srcFx)) params.setBool("source_fx_enabled", srcFx);
			int srcMode = params.getInt("source_fx_mode", kRepeatFxMode);
			if (ImGui::Combo("Source FX Mode", &srcMode, fxModes, 7)) params.setInt("source_fx_mode", srcMode);
			drawFxKnobs("source", srcMode, "source_fx_param1", "source_fx_param2", "source_fx_param3",
			            "source_fx_count", "source_fx_repeat_blend", 0.12f, 0.0f, 0.0f, 4);

			ImGui::Separator();
			ImGui::TextUnformatted("Web interface");
			ImGui::Separator();
			bool ipad = params.getBool("ipad_page", false);
			if (ImGui::Checkbox("Web interface", &ipad)) params.setBool("ipad_page", ipad);
			bool token = params.getBool("ipad_token", true);
			if (ImGui::Checkbox("URL token", &token)) params.setBool("ipad_token", token);
			float silence = params.getFloat("silence_ms", 300.0f);
			if (ImGui::SliderFloat("Silence window (ms)", &silence, 50.0f, 2000.0f))
				params.setFloat("silence_ms", silence);
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
			bool show = params.getBool("show_ui", true);
			if (ImGui::Checkbox("Show UI", &show)) params.setBool("show_ui", show);
			bool separate = params.getBool("preview_window", false);
			if (ImGui::Checkbox("Separate Preview Window", &separate)) params.setBool("preview_window", separate);
			float pscale = params.getFloat("preview_scale", 0.3f);
			if (ImGui::SliderFloat("Preview Scale", &pscale, 0.1f, 0.6f)) params.setFloat("preview_scale", pscale);
			bool showPrev = params.getBool("show_preview", true);
			if (ImGui::Checkbox("Show Preview", &showPrev)) params.setBool("show_preview", showPrev);
			bool showHeads = params.getBool("show_playheads", true);
			if (ImGui::Checkbox("Show Playheads", &showHeads)) params.setBool("show_playheads", showHeads);
			bool previewBack = params.getBool("debug_preview_backbuffer", false);
			if (ImGui::Checkbox("Preview Backbuffer", &previewBack))
				params.setBool("debug_preview_backbuffer", previewBack);

			ImGui::Separator();
			ImGui::TextUnformatted("Link");
			ImGui::Separator();
			bool linkEnabledUi = params.getBool("link_enabled", false);
			if (ImGui::Checkbox("Ableton Link", &linkEnabledUi)) params.setBool("link_enabled", linkEnabledUi);
			int peers = link.getNumPeers();
			ImGui::Text("Link Peers: %d", peers);
			bool ss = params.getBool("link_startstop", false);
			if (ImGui::Checkbox("Link Start/Stop Sync", &ss)) params.setBool("link_startstop", ss);
			bool lpUi = params.getBool("link_playing", false);
			if (ImGui::Checkbox("Link Playing", &lpUi)) params.setBool("link_playing", lpUi);

			ImGui::Separator();
			ImGui::TextUnformatted("Tempo & Speed");
			ImGui::Separator();
			float bpmVal = params.getFloat("bpm", 120.0f);
			if (ImGui::SliderFloat("BPM", &bpmVal, 40.0f, 240.0f)) {
				params.setFloat("bpm", bpmVal);
				if (params.getBool("link_enabled", false)) {
					link.setTempo(bpmVal);
				}
			}
			float qVal = params.getFloat("quantum", 4.0f);
			if (ImGui::SliderFloat("Quantum", &qVal, 1.0f, 16.0f)) params.setFloat("quantum", qVal);
			bool tempo = params.getBool("tempo_coupled", false);
			if (ImGui::Checkbox("Tempo Coupled Read", &tempo)) params.setBool("tempo_coupled", tempo);
			float mult = params.getFloat("read_speed_mult", 1.0f);
			if (ImGui::SliderFloat("Read Speed Mult", &mult, 0.25f, 4.0f)) params.setFloat("read_speed_mult", mult);

			ImGui::Separator();
			ImGui::TextUnformatted("Debug");
			ImGui::Separator();
			int readBank = ring.getReadBank();
			int writeBank = ring.getWriteBank();
			ImGui::Text("Current Read Buffer: %c", readBank == 0 ? 'A' : 'B');
			ImGui::Text("Current Write Buffer: %c", writeBank == 0 ? 'A' : 'B');
			ImGui::Separator();
			ImGui::Text("Ableton Link SDK: %s", link.isAvailable() ? "Loaded" : "Not compiled in");

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	outBpm = params.getFloat("bpm", 120.0f);
	outQuantum = params.getFloat("quantum", 4.0f);

	ImGui::End();
}
