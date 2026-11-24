#pragma once

#include "io/FolderList.hpp"
#include "ui/ParamBus.hpp"

#include <filesystem>
#include <string>
#include <vector>

// The performance setup that comes back at launch: the controls the control table marks Saved, and the Folder
// list. The Loop's frames are not part of it. values is a whole ParamBus; only the Saved controls in it count.
struct PerformanceSetup {
	ParamBus values;
	std::vector<std::string> folders;
};

// paths keeps the saved folders that are still directories, in order.
// current is the saved Videos folder when it can still be read.
struct RestoredFolders {
	std::vector<std::string> paths;
	std::string current;
};

PerformanceSetup captureSetup(const ParamBus& params, const FolderList& folders);
// Controls the table marks StartsOff (Latch, latched Cue, Export) go back to off. Other controls outside the
// setup are left as they are.
void applySetup(ParamBus& params, const PerformanceSetup& setup);
bool writeSetup(const std::filesystem::path& path, const PerformanceSetup& setup);
bool readSetup(const std::filesystem::path& path, PerformanceSetup& setup);
RestoredFolders restoreFolders(const PerformanceSetup& setup);

// Writes once the setup has stopped changing, which is a control being released.
class SetupSaver {
public:
	SetupSaver(std::filesystem::path path, double quietSeconds);
	void settle(const PerformanceSetup& setup);
	void observe(const PerformanceSetup& setup, double nowSeconds);
	void flush();

private:
	std::filesystem::path file;
	double quiet;
	std::string saved;
	std::string pending;
	double since = 0;
	bool waiting = false;
};
