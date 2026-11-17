#pragma once

#include <string>
#include <functional>

// Shows a native macOS file open dialog. Returns an absolute file path or empty string if cancelled.
std::string macOpenFileDialog(const char* title = "Open File");

// Shows a native macOS directory open dialog. Returns selected directory path or empty string if cancelled.
std::string macOpenDirectoryDialog(const char* title = "Select Folder");

// Non-blocking variants. Invoke the callback on completion with the selected path (or empty on cancel).
void macOpenFileDialogAsync(const char* title, std::function<void(std::string)> onPick);
void macOpenDirectoryDialogAsync(const char* title, std::function<void(std::string)> onPick);

// Shows a native macOS save file dialog. Returns selected path or empty string on cancel.
std::string macSaveFileDialog(const char* title = "Save File", const char* defaultName = "output.mov");
// Non-blocking variant for save dialog
void macSaveFileDialogAsync(const char* title, const char* defaultName, std::function<void(std::string)> onPick);
