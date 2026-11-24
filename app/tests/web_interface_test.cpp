#include "control/Instrument.hpp"
#include "io/FolderList.hpp"
#include "io/VideoSource.hpp"
#include "remote/MemoryWire.hpp"
#include "remote/WebInterface.hpp"
#include "ui/ParamBus.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) ++failures;
}

static std::string setFrame(const std::string& name, const std::string& raw) {
	return "{\"t\":\"set\",\"name\":\"" + name + "\",\"value\":" + raw + "}";
}

struct FakeSource : SourcePort {
	std::string loaded;
	std::string dubbed;
	std::vector<std::string> page;
	int webcam = -1;
	void loadVideo(const std::string& path, float, float) override { loaded = path; }
	void queueDub(const std::string& path, float, float) override { dubbed = path; }
	void primePage(const std::vector<std::string>& paths, float, float) override { page = paths; }
	void loadWebcam(int index) override { webcam = index; }
	void choose(SourceKind) override {}
	void setPlayback(bool, bool, float, float) override {}
	void restartFileAtLoopStart() override {}
	SourceStatus status() const override { return {}; }
};

struct Rig {
	ParamBus params;
	FakeSource source;
	FolderList folders;
	float linkTempo = -1;
	Instrument instrument;
	MemoryWire wire;
	WebInterface web;

	explicit Rig(std::filesystem::path usage = {})
		: instrument(params, source, folders, [this](float bpm) { linkTempo = bpm; }),
		  web(wire, std::move(usage)) {}

	WebInterface::Step frame(const WebLive& extra) {
		WebLive live = extra;
		live.enabled = true;
		return web.frame(instrument, live);
	}

	WebInterface::Step frame() {
		WebLive live{params, folders};
		return frame(live);
	}
};

static bool sentHas(const MemoryWire& wire, const std::string& bit) {
	for (const std::string& frame : wire.sent())
		if (frame.find(bit) != std::string::npos) return true;
	return false;
}

static const std::string& lastSnapshot(const MemoryWire& wire) {
	for (auto it = wire.sent().rbegin(); it != wire.sent().rend(); ++it)
		if (it->rfind("{\"t\":\"snapshot\"", 0) == 0) return *it;
	static const std::string empty;
	return empty;
}

static int rejects(const MemoryWire& wire) {
	int n = 0;
	for (const std::string& frame : wire.sent())
		if (frame.rfind("{\"t\":\"reject\"", 0) == 0) ++n;
	return n;
}

int main() {
	auto dir = std::filesystem::temp_directory_path() / ("vl-web-" + std::to_string(getpid()));
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);

	{
		Rig rig;
		rig.params.setInt("webcam_index", 2);
		rig.params.setInt("source_kind", int(SourceKind::File));
		rig.wire.push(R"({"t":"webcam"})");
		rig.frame();
		check(rig.source.webcam < 0 && rig.params.getInt("source_kind") == int(SourceKind::File),
		      "the webcam message the page never sends is not accepted");
		rig.wire.push(setFrame("source_kind", "3"));
		rig.frame();
		check(rig.source.webcam == 2 && rig.params.getInt("source_kind") == int(SourceKind::File),
		      "source_kind 3 asks for a webcam load and leaves the Source kind");
	}
	{
		Rig rig;
		rig.params.setBool("arm_record", true);
		rig.params.setBool("cue_active", true);
		rig.params.setFloat("bpm", 128);
		rig.wire.push(R"({"t":"hold","name":"gate"})");
		rig.wire.push(R"({"t":"hold","name":"cue"})");
		rig.wire.push(R"({"t":"hold","name":"erase"})");
		WebInterface::Step down = rig.frame();
		check(down.gate && down.cue && down.erase, "the frame call reads back the held pads");
		rig.wire.push(R"({"t":"panic"})");
		WebInterface::Step up = rig.frame();
		check(!up.gate && !up.cue && !up.erase && !rig.params.getBool("arm_record") &&
		          !rig.params.getBool("cue_active") && rig.params.getFloat("bpm") == 128.0f,
		      "panic releases the Latch, the latched Cue, and every held pad");
	}
	{
		Rig rig;
		rig.folders.add({"/v", {"/v/a.mov", "/v/b.mov"}});
		rig.folders.add({"/w", {"/w/c.mov"}});
		rig.folders.select("/v");
		WebLive live{rig.params, rig.folders};
		rig.frame(live);
		const std::string& first = lastSnapshot(rig.wire);
		check(first.find(R"("videos":["a.mov","b.mov"])") != std::string::npos &&
		          first.find(R"("folders":["/v","/w"])") != std::string::npos,
		      "the first frame sends the Videos and the Folder list");
		rig.frame(live);
		const std::string& omitted = lastSnapshot(rig.wire);
		check(omitted.find("\"videos\"") == std::string::npos && omitted.find("\"folders\"") == std::string::npos,
		      "the next frame omits the catalog");
		size_t sent = rig.wire.sent().size();
		rig.frame(live);
		check(rig.wire.sent().size() == sent, "an unchanged frame sends nothing");
		live.loop = 0.25f;
		rig.frame(live);
		const std::string& hot = lastSnapshot(rig.wire);
		check(hot.find("\"videos\"") == std::string::npos && hot.find("\"folders\"") == std::string::npos &&
		          hot.find("\"loop\":") != std::string::npos,
		      "a later frame omits the catalog");
		rig.wire.push(R"({"t":"folder","name":"/w"})");
		live.loop = 0.5f;
		rig.frame(live);
		check(lastSnapshot(rig.wire).find(R"("videos":["c.mov"])") != std::string::npos &&
		          rig.params.getString("videos_folder") == "/w",
		      "choosing a folder sends that folder's Videos");
		rig.wire.push(R"({"t":"folder","name":"/nowhere"})");
		rig.frame(live);
		check(sentHas(rig.wire, "\"reason\":\"no such folder\"") && rig.params.getString("videos_folder") == "/w" &&
		          rig.folders.current()->path == "/w",
		      "an unknown folder is rejected and the Videos folder stays");
	}
	{
		Rig rig;
		rig.params.setInt("source_kind", int(SourceKind::File));
		WebLive live{rig.params, rig.folders};
		live.loadFailure = "could not open";
		rig.frame(live);
		check(sentHas(rig.wire, "\"reason\":\"could not open\"") &&
		          lastSnapshot(rig.wire).find(R"("source_error":"could not open")") != std::string::npos,
		      "a load failure is rejected and written on the snapshot");
		int once = rejects(rig.wire);
		live.loadFailure.clear();
		live.loop = 0.2f;
		rig.frame(live);
		check(rejects(rig.wire) == once &&
		          lastSnapshot(rig.wire).find(R"("source_error":"could not open")") != std::string::npos,
		      "the load failure stays until the load swaps in");
		live.loadSwapped = true;
		live.loop = 0.4f;
		rig.frame(live);
		check(lastSnapshot(rig.wire).find(R"("source_error":"")") != std::string::npos,
		      "a swapped load clears the failure");
		live.loadSwapped = false;
		live.loadFailure = "could not open";
		rig.frame(live);
		rig.params.setInt("source_kind", int(SourceKind::Shader));
		live.loadFailure.clear();
		live.loop = 0.6f;
		rig.frame(live);
		check(lastSnapshot(rig.wire).find(R"("source_error":"")") != std::string::npos,
		      "leaving Video and webcam clears the load failure");
	}
	{
		Rig rig;
		rig.instrument.addFolder("/v", {"/v/a.mov", "/v/b.mov"});
		rig.params.setString("file_path", "/v/a.mov");
		rig.params.setInt("source_kind", int(SourceKind::File));
		rig.wire.push(R"({"t":"next"})");
		rig.frame();
		check(rig.source.loaded == "/v/b.mov", "next asks the Source to load the next Video");
		rig.wire.push(R"({"t":"load","name":"a.mov"})");
		rig.frame();
		check(rig.source.loaded == "/v/a.mov" && rig.source.dubbed.empty(),
		      "a tap loads a.mov and does not Commit");
		rig.wire.push(R"({"t":"load","name":"zzz.mov"})");
		rig.frame();
		check(sentHas(rig.wire, "\"reason\":\"no such video\"") && rig.source.loaded == "/v/a.mov",
		      "an unknown Video is rejected and the loaded one stays");
		rig.wire.push(R"({"t":"dub","name":"b.mov"})");
		rig.frame();
		check(rig.source.dubbed == "/v/b.mov", "a Dub asks the Source to Commit b.mov");
		rig.wire.push(R"({"t":"page","names":["a.mov","b.mov"]})");
		rig.frame();
		check(rig.source.page == std::vector<std::string>{"/v/a.mov", "/v/b.mov"},
		      "a Page asks the Source to open those Videos");
		rig.wire.push(R"({"t":"page","names":[]})");
		rig.frame();
		check(rig.source.page.empty(), "an empty page opens nothing");
	}
	{
		Rig rig;
		WebLive live{rig.params, rig.folders};
		live.exportFailure = "could not start";
		rig.frame(live);
		check(sentHas(rig.wire, "\"name\":\"record_enabled\"") &&
		          sentHas(rig.wire, "\"reason\":\"could not start\"") &&
		          lastSnapshot(rig.wire).find(R"("export_error":"could not start")") != std::string::npos,
		      "an Export failure is rejected and written on the snapshot");
		int once = rejects(rig.wire);
		live.loop = 0.1f;
		rig.frame(live);
		check(rejects(rig.wire) == once, "the same Export failure is not rejected again");
	}
	{
		auto log = dir / "web-use.jsonl";
		Rig rig(log);
		rig.wire.push(setFrame("fx_mode", "5"));
		rig.wire.push(R"({"t":"use","e":"set","name":"fx_mode","v":5})");
		rig.wire.push(R"({"t":"use","e":"act","name":"dub","v":"clip.mp4"})");
		rig.wire.push(R"({"t":"use","e":"hold","name":"gate"})");
		rig.wire.push(R"({"t":"use","e":"nope","name":"x"})");
		rig.wire.push(R"({"t":"use","name":"fx_mode"})");
		rig.frame();
		std::ifstream in(log);
		std::string data((std::istreambuf_iterator<char>(in)), {});
		check(rig.params.getInt("fx_mode") == 5 && data.find("\"e\":\"set\"") != std::string::npos &&
		          data.find("\"v\":5") != std::string::npos,
		      "a use gesture is appended and the control set is carried out");
		check(data.find("\"v\":\"clip.mp4\"") != std::string::npos, "a dub name is kept on the gesture");
		int fxLines = 0;
		for (size_t at = 0; (at = data.find("fx_mode", at)) != std::string::npos; at += 7)
			++fxLines;
		check(fxLines == 1, "a control set is not logged unless the page sends use");
		check(data.find("nope") == std::string::npos, "an unknown gesture is not stored");
	}
	{
		Rig rig;
		rig.wire.pushDraw("png-one");
		WebInterface::Step step = rig.frame();
		check(step.hasDraw && step.draw == "png-one", "the frame call returns the Draw picture");
		rig.wire.pushDraw("newer");
		rig.wire.pushDraw("newest");
		step = rig.frame();
		check(step.hasDraw && step.draw == "newest", "a newer Draw picture replaces one still waiting");
		step = rig.frame();
		check(!step.hasDraw, "the picture is taken once");
	}
	{
		Rig rig;
		rig.wire.pushDraw("bytes");
		WebLive live{rig.params, rig.folders};
		live.thumbsFresh = true;
		live.thumbs = {{"a.mov", "jpeg"}};
		WebInterface::Step step = rig.frame(live);
		check(step.hasDraw && step.draw == "bytes" && rig.wire.thumb("a.mov") == "jpeg",
		      "one frame hands the thumbnails to the wire and reads the Draw picture back");
	}

	std::filesystem::remove_all(dir);
	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
