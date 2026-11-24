#pragma once

#include <string>
#include <vector>

// Decodes one Draw PNG into straight-alpha BGRA, bottom row first, the same layout as a Video frame.
// Rejects an empty payload, a payload over 4 MB, and an image wider or taller than 2048.
bool decodeDrawImage(const std::string& png, std::vector<unsigned char>& bgra, int& width, int& height);
