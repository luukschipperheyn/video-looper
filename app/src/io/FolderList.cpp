#include "io/FolderList.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

void FolderList::add(PreparedFolder folder) {
	for (PreparedFolder& f : folders) {
		if (f.path == folder.path) {
			f = std::move(folder);
			return;
		}
	}
	folders.push_back(std::move(folder));
}

bool FolderList::select(const std::string& path) {
	for (size_t i = 0; i < folders.size(); ++i) {
		if (folders[i].path == path) {
			currentIndex = int(i);
			return true;
		}
	}
	return false;
}

const PreparedFolder* FolderList::current() const {
	return currentIndex < 0 ? nullptr : &folders[size_t(currentIndex)];
}

std::vector<std::string> FolderList::paths() const {
	std::vector<std::string> out;
	for (const PreparedFolder& f : folders) out.push_back(f.path);
	return out;
}

std::vector<std::string> scanVideos(const std::string& folder) {
	std::vector<std::string> out;
	std::error_code ec;
	for (auto const& de : std::filesystem::directory_iterator(folder, ec)) {
		if (!de.is_regular_file(ec)) continue;
		std::string ext = de.path().extension().string();
		for (auto& c : ext) c = (char)std::tolower(static_cast<unsigned char>(c));
		if (ext == ".mp4" || ext == ".mov" || ext == ".m4v" || ext == ".mkv" || ext == ".avi") {
			out.push_back(de.path().string());
		}
	}
	std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
		return std::filesystem::path(a).filename() < std::filesystem::path(b).filename();
	});
	return out;
}
