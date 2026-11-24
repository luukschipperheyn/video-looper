#pragma once

#include <functional>
#include <string>
#include <vector>

class FolderList;
class ParamBus;

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
	virtual void showFolder(const std::string& path, const std::vector<std::string>& videos, ParamBus& params) = 0;
	// Full paths of the Videos in the current Videos folder.
	virtual std::vector<std::string> videos() const = 0;
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
	// An Int control is rounded. Choosing the webcam as the Source kind starts a webcam load and leaves
	// the Source kind as it is until that load swaps in.
	Outcome setNumber(const std::string& name, double value);
	Outcome setText(const std::string& name, const std::string& text);

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

private:
	float loopStart() const;
	float loopLength() const;
	void step(int delta);

	ParamBus& params;
	SourcePort& source;
	FolderList& folders;
	Tempo tempo;
};
