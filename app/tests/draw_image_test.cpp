#include "io/DrawImage.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) ++failures;
}

static bool near(unsigned char got, unsigned char want) {
	int d = int(got) - int(want);
	return d < 0 ? -d <= 2 : d <= 2;
}

int main() {
	std::ifstream in(DRAW_PNG, std::ios::binary);
	std::string png((std::istreambuf_iterator<char>(in)), {});
	std::vector<unsigned char> bgra;
	int width = 0;
	int height = 0;
	check(decodeDrawImage(png, bgra, width, height) && width == 2 && height == 2 && bgra.size() == 16,
	      "a 2x2 Draw PNG decodes");
	if (bgra.size() == 16) {
		// Bottom row first. Bottom-left is straight green at alpha 128. Bottom-right is opaque blue.
		check(bgra[0] == 0 && near(bgra[1], 255) && bgra[2] == 0 && near(bgra[3], 128),
		      "bottom-left is straight green, not premultiplied");
		check(bgra[4] == 255 && bgra[5] == 0 && bgra[6] == 0 && bgra[7] == 255, "bottom-right is opaque blue");
		check(bgra[8] == 0 && bgra[9] == 0 && bgra[10] == 255 && bgra[11] == 255, "top-left is opaque red");
		check(bgra[12] == 0 && bgra[13] == 0 && bgra[14] == 0 && bgra[15] == 0, "top-right is transparent");
		if (failures) {
			std::printf("pixels");
			for (unsigned char c : bgra) std::printf(" %u", c);
			std::printf("\n");
		}
	}
	std::string junk(100, '\0');
	check(!decodeDrawImage(junk, bgra, width, height) && bgra.empty(), "a non-image is rejected");
	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
