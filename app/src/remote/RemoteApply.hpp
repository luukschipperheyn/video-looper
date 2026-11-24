#pragma once

#include <string>
#include <vector>

class Instrument;
class ParamBus;

enum class RemoteKind { Set, Panic, Prev, Next, Webcam, Load, Folder, Dub, Page };

// One message from the iPad for the main thread. For a Set, raw is the JSON value token as sent: true, false,
// 140, or "x". A Load or Dub carries the Video's filename as name, a Folder the Videos folder's full path. A Page
// carries the filenames of the Videos on screen in names. The other kinds carry no name or value.
struct RemoteSet {
	std::string name;
	std::string raw;
	RemoteKind kind = RemoteKind::Set;
	std::vector<std::string> names;
};

// This frame's loop position as a fraction of the Loop, the envelope, and the combined holds.
struct RemoteLive {
	float loop = 0;
	float envelope = 0;
	int stage = 0;
	bool gate = false;
	bool cue = false;
	bool erase = false;
};

// Carries out one message from the Web interface. A Set is refused as "read-only" when the Web interface may
// not write that control, and as "bad value" when the value does not parse. Returns the reject frame to send
// back, or empty when the message was carried out. source_kind Draw is written like Shader and Black; Webcam
// asks for a load and is not written.
std::string applyRemote(Instrument& instrument, const RemoteSet& set);
// videos are the filenames of the Videos in the current Videos folder; folders are the Folder list's full paths.
// catalog false omits those lists, for the per-frame update. A new client still gets them from the stored snapshot.
std::string remoteSnapshot(const ParamBus& params, const RemoteLive& live = {},
                           const std::vector<std::string>& videos = {}, const std::vector<std::string>& folders = {},
                           bool catalog = true, const std::string& sourceError = {});
std::string rejectFrame(const std::string& name, const std::string& reason);
