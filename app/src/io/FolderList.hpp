#pragma once

#include <string>
#include <vector>

struct PreparedFolder {
	std::string path;
	std::vector<std::string> videos; // full paths, captured at add
};

// The Videos folders prepared before a set, in order. Selecting one never reads the disk.
class FolderList {
public:
	// Appends folder, or replaces the listed folder with the same path in place.
	void add(PreparedFolder folder);
	// Makes the listed folder at path current. False if path is not listed.
	bool select(const std::string& path);
	const PreparedFolder* current() const;
	std::vector<std::string> paths() const;

private:
	std::vector<PreparedFolder> folders;
	int currentIndex = -1;
};

// Full paths of the Videos directly in folder, sorted by filename. Empty if the folder cannot be read.
std::vector<std::string> scanVideos(const std::string& folder);
