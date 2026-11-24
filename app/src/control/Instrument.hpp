#pragma once

#include <functional>
#include <string>
#include <vector>

#include "io/VideoSource.hpp"

class FolderList;
class ParamBus;

// What the Source last became, for the Instrument to write into the controls.
struct SourceStatus {
	SourceKind kind = SourceKind::Shader;
	std::string path;
	int webcamIndex = 0;
	bool swapped = false;
};

// What the instrument asks of the Source. SourceController is the real one.
class SourcePort {
public:
	virtual ~SourcePort() = default;
	virtual void loadVideo(const std::string& path, float loopStart, float loopLength) = 0;
	// Selects path and Commits it into the Loop once it is the Source.
	virtual void queueDub(const std::string& path, float loopStart, float loopLength) = 0;
	// Opens these Videos ahead of a tap. paths are full paths. An empty list drops the page.
	virtual void primePage(const std::vector<std::string>& paths, float loopStart, float loopLength) = 0;
	virtual void loadWebcam(int index) = 0;
	// Makes path the current Videos folder with videos as its Videos, without reading the disk.
	virtual void showFolder(const std::string& path, const std::vector<std::string>& videos) = 0;
	// Full paths of the Videos in the current Videos folder.
	virtual std::vector<std::string> videos() const = 0;
	// Shader, Black, or Draw. Drops a load that has not swapped in.
	virtual void choose(SourceKind kind) = 0;
	virtual void setPlayback(bool loop, bool paused, float loopStart, float loopLength) = 0;
	virtual void restartFileAtLoopStart() = 0;
	virtual SourceStatus status() const = 0;
};

// ok false: nothing changed. topic is the control's name, "source", or "folder".
struct Outcome {
	bool ok = true;
	std::string topic;
	std::string reason;
};

// Carries out what a performer asks for, whichever surface it came from: the Mac controls, the hotkeys, or the
// Web interface. Setting a control clamps it to the control table's range and does what follows from it.
class Instrument {
public:
	// tempo is called with the new bpm when the tempo is set while Link is on.
	using Tempo = std::function<void(float)>;
	Instrument(ParamBus& params, SourcePort& source, FolderList& folders, Tempo tempo);

	// Refused with "bad value" when the control is not of that type or is not in the control table.
	// Turning Export on with no export path is refused with "no export path".
	// Turning Preserve aspect on copies scale x onto scale y.
	Outcome setSwitch(const std::string& name, bool on);
	// An Int control is rounded. Webcam starts a webcam load and leaves the Source kind until that load
	// swaps in. File is written, and loads file_path when the Source is not already a Video and the path
	// is set. Shader, Black, and Draw are written and chosen.
	Outcome setNumber(const std::string& name, double value);
	Outcome setText(const std::string& name, const std::string& text);
	// Adds delta to an Int or Float control and sets it. The control table's clamp is the end stop.
	// Refused the same way setNumber refuses a bad control. source_kind is refused.
	Outcome nudge(const std::string& name, double delta);
	// Flips a Bool control. Refused when it is not a Bool in the table.
	Outcome toggle(const std::string& name);
	// Cycles Shader, File, Black, Webcam. Draw stays out. Plus on Webcam or Draw does nothing. Minus on
	// Draw sets Black. Any other plus or minus is setNumber of the kind one step that way.
	void stepSourceKind(int delta);

	// Releases the Latch and the latched Cue.
	void panic();
	void prevVideo();
	void nextVideo();
	void randomVideo();
	void loadVideo(const std::string& path);
	// name is a filename in the current Videos folder. Refused with "no such video".
	Outcome loadVideoNamed(const std::string& name);
	Outcome dubVideoNamed(const std::string& name);
	void primePage(const std::vector<std::string>& names);
	// path is a folder in the Folder list. Refused with "no such folder".
	Outcome chooseFolder(const std::string& path);
	// Adds the folder to the Folder list, or replaces it there, and makes it current.
	void addFolder(const std::string& path, const std::vector<std::string>& videos);
	void chooseWebcam();
	// When status.swapped, writes source_kind and file_path, and webcam_index when the kind is Webcam.
	void record(const SourceStatus& status);
	// Restarts the live Video when file_run_mode is Trigger and the Source kind is File.
	void restartVideo();
	// Asks the Source for the Video, webcam, Shader, Black, or Draw the controls already hold.
	void restoreSource();

private:
	float loopStart() const;
	float loopLength() const;
	void applyPlayback();
	void step(int delta);

	ParamBus& params;
	SourcePort& source;
	FolderList& folders;
	Tempo tempo;
};
