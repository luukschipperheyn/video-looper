#pragma once

#include <string>
#include <vector>

// Square QR module grid (true = dark, indexed [row][column]) encoding text in byte mode at
// error correction level L. Supports versions 1 to 5, so text up to 106 bytes; longer text
// returns an empty grid.
std::vector<std::vector<bool>> qrEncode(const std::string& text);
