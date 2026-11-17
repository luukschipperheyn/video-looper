#import <Cocoa/Cocoa.h>

#include <string>
#include <functional>

std::string macOpenFileDialog(const char* title) {
	@autoreleasepool {
		NSOpenPanel* panel = [NSOpenPanel openPanel];
		[panel setAllowsMultipleSelection:NO];
		[panel setCanChooseDirectories:NO];
		[panel setCanChooseFiles:YES];
		[panel setTitle:[NSString stringWithUTF8String:title ? title : "Open File"]];
		NSInteger result = [panel runModal];
		if (result == NSModalResponseOK) {
			NSURL* url = [[panel URLs] firstObject];
			if (url) {
				return std::string([[url path] UTF8String]);
			}
		}
	}
	return std::string();
}


std::string macOpenDirectoryDialog(const char* title) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setAllowsMultipleSelection:NO];
        [panel setCanChooseDirectories:YES];
        [panel setCanChooseFiles:NO];
        [panel setTitle:[NSString stringWithUTF8String:title ? title : "Select Folder"]];
        NSInteger result = [panel runModal];
        if (result == NSModalResponseOK) {
            NSURL* url = [[panel URLs] firstObject];
            if (url) {
                return std::string([[url path] UTF8String]);
            }
        }
    }
    return std::string();
}


// Non-blocking variants using beginWithCompletionHandler
void macOpenFileDialogAsync(const char* title, std::function<void(std::string)> onPick) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setAllowsMultipleSelection:NO];
        [panel setCanChooseDirectories:NO];
        [panel setCanChooseFiles:YES];
        [panel setTitle:[NSString stringWithUTF8String:title ? title : "Open File"]];

        // Retain the callback until completion using shared ownership
        auto callbackHolder = std::make_shared<std::function<void(std::string)>>(std::move(onPick));
        [panel beginWithCompletionHandler:^(NSModalResponse result) {
            std::string path;
            if (result == NSModalResponseOK) {
                NSURL* url = [[panel URLs] firstObject];
                if (url) {
                    path = std::string([[url path] UTF8String]);
                }
            }
            if (callbackHolder && static_cast<bool>(*callbackHolder)) { (*callbackHolder)(path); }
        }];
    }
}

void macOpenDirectoryDialogAsync(const char* title, std::function<void(std::string)> onPick) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setAllowsMultipleSelection:NO];
        [panel setCanChooseDirectories:YES];
        [panel setCanChooseFiles:NO];
        [panel setTitle:[NSString stringWithUTF8String:title ? title : "Select Folder"]];

        auto callbackHolder = std::make_shared<std::function<void(std::string)>>(std::move(onPick));
        [panel beginWithCompletionHandler:^(NSModalResponse result) {
            std::string path;
            if (result == NSModalResponseOK) {
                NSURL* url = [[panel URLs] firstObject];
                if (url) {
                    path = std::string([[url path] UTF8String]);
                }
            }
            if (callbackHolder && static_cast<bool>(*callbackHolder)) { (*callbackHolder)(path); }
        }];
    }
}


std::string macSaveFileDialog(const char* title, const char* defaultName) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setTitle:[NSString stringWithUTF8String:title ? title : "Save File"]];
        if (defaultName && defaultName[0] != '\0') {
            [panel setNameFieldStringValue:[NSString stringWithUTF8String:defaultName]];
        }
        NSInteger result = [panel runModal];
        if (result == NSModalResponseOK) {
            NSURL* url = [panel URL];
            if (url) {
                return std::string([[url path] UTF8String]);
            }
        }
    }
    return std::string();
}

void macSaveFileDialogAsync(const char* title, const char* defaultName, std::function<void(std::string)> onPick) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setTitle:[NSString stringWithUTF8String:title ? title : "Save File"]];
        if (defaultName && defaultName[0] != '\0') {
            [panel setNameFieldStringValue:[NSString stringWithUTF8String:defaultName]];
        }
        auto callbackHolder = std::make_shared<std::function<void(std::string)>>(std::move(onPick));
        [panel beginWithCompletionHandler:^(NSModalResponse result) {
            std::string path;
            if (result == NSModalResponseOK) {
                NSURL* url = [panel URL];
                if (url) {
                    path = std::string([[url path] UTF8String]);
                }
            }
            if (callbackHolder && static_cast<bool>(*callbackHolder)) { (*callbackHolder)(path); }
        }];
    }
}

