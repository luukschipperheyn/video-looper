#include "remote/QrCode.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace {

struct Grid {
	int size;
	std::vector<std::vector<bool>> dark;
	std::vector<std::vector<bool>> function;
	void set(int x, int y, bool d) {
		dark[y][x] = d;
		function[y][x] = true;
	}
};

uint8_t gfMul(uint8_t x, uint8_t y) {
	int z = 0;
	for (int i = 7; i >= 0; --i) {
		z = (z << 1) ^ ((z >> 7) * 0x11d);
		z ^= ((y >> i) & 1) * x;
	}
	return uint8_t(z);
}

std::vector<uint8_t> reedSolomon(const std::vector<uint8_t>& data, int degree) {
	std::vector<uint8_t> divisor(degree, 0);
	divisor[degree - 1] = 1;
	uint8_t root = 1;
	for (int i = 0; i < degree; ++i) {
		for (int j = 0; j < degree; ++j) {
			divisor[j] = gfMul(divisor[j], root);
			if (j + 1 < degree) divisor[j] ^= divisor[j + 1];
		}
		root = gfMul(root, 2);
	}
	std::vector<uint8_t> rem(degree, 0);
	for (uint8_t b : data) {
		uint8_t factor = b ^ rem[0];
		rem.erase(rem.begin());
		rem.push_back(0);
		for (int i = 0; i < degree; ++i) rem[i] ^= gfMul(divisor[i], factor);
	}
	return rem;
}

void finder(Grid& g, int cx, int cy) {
	for (int dy = -4; dy <= 4; ++dy) {
		for (int dx = -4; dx <= 4; ++dx) {
			int x = cx + dx, y = cy + dy;
			if (x < 0 || y < 0 || x >= g.size || y >= g.size) continue;
			int d = std::max(std::abs(dx), std::abs(dy));
			g.set(x, y, d != 2 && d != 4);
		}
	}
}

void alignment(Grid& g, int cx, int cy) {
	for (int dy = -2; dy <= 2; ++dy)
		for (int dx = -2; dx <= 2; ++dx) g.set(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
}

void drawFormat(Grid& g, int mask) {
	int data = 1 << 3 | mask;
	int rem = data;
	for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
	int bits = (data << 10 | rem) ^ 0x5412;
	auto bit = [&](int i) { return ((bits >> i) & 1) != 0; };
	for (int i = 0; i <= 5; ++i) g.set(8, i, bit(i));
	g.set(8, 7, bit(6));
	g.set(8, 8, bit(7));
	g.set(7, 8, bit(8));
	for (int i = 9; i < 15; ++i) g.set(14 - i, 8, bit(i));
	for (int i = 0; i < 8; ++i) g.set(g.size - 1 - i, 8, bit(i));
	for (int i = 8; i < 15; ++i) g.set(8, g.size - 15 + i, bit(i));
	g.set(8, g.size - 8, true);
}

bool maskBit(int mask, int x, int y) {
	switch (mask) {
	case 0:
		return (x + y) % 2 == 0;
	case 1:
		return y % 2 == 0;
	case 2:
		return x % 3 == 0;
	case 3:
		return (x + y) % 3 == 0;
	case 4:
		return (x / 3 + y / 2) % 2 == 0;
	case 5:
		return x * y % 2 + x * y % 3 == 0;
	case 6:
		return (x * y % 2 + x * y % 3) % 2 == 0;
	default:
		return ((x + y) % 2 + x * y % 3) % 2 == 0;
	}
}

int penalty(const std::vector<std::vector<bool>>& m) {
	int n = int(m.size()), score = 0, dark = 0;
	for (int rows = 0; rows < 2; ++rows) {
		for (int i = 0; i < n; ++i) {
			int run = 0, window = 0;
			for (int j = 0; j < n; ++j) {
				bool cur = rows ? m[i][j] : m[j][i];
				bool prev = j > 0 && (rows ? m[i][j - 1] : m[j - 1][i]);
				run = (j > 0 && cur == prev) ? run + 1 : 1;
				if (run == 5) score += 3;
				else if (run > 5) score += 1;
				window = ((window << 1) | cur) & 0x7ff;
				if (j >= 10 && (window == 0x5d0 || window == 0x05d)) score += 40;
			}
		}
	}
	for (int y = 0; y < n; ++y) {
		for (int x = 0; x < n; ++x) {
			dark += m[y][x];
			if (x + 1 < n && y + 1 < n && m[y][x] == m[y][x + 1] && m[y][x] == m[y + 1][x] &&
			    m[y][x] == m[y + 1][x + 1])
				score += 3;
		}
	}
	return score + std::abs(dark * 100 / (n * n) - 50) / 5 * 10;
}

} // namespace

std::vector<std::vector<bool>> qrEncode(const std::string& text) {
	const int totalCodewords[] = {26, 44, 70, 100, 134};
	const int eccCodewords[] = {7, 10, 15, 20, 26};
	int version = 0;
	for (int v = 1; v <= 5 && !version; ++v)
		if (int(text.size()) + 2 <= totalCodewords[v - 1] - eccCodewords[v - 1]) version = v;
	if (!version) return {};
	int dataCodewords = totalCodewords[version - 1] - eccCodewords[version - 1];

	std::vector<bool> bits;
	auto put = [&](uint32_t value, int count) {
		for (int i = count - 1; i >= 0; --i) bits.push_back((value >> i) & 1);
	};
	put(4, 4);
	put(uint32_t(text.size()), 8);
	for (char c : text) put(uint8_t(c), 8);
	int capacity = dataCodewords * 8;
	put(0, std::min(4, capacity - int(bits.size())));
	put(0, int((8 - bits.size() % 8) % 8));
	for (uint32_t pad = 0xec; int(bits.size()) < capacity; pad ^= 0xec ^ 0x11) put(pad, 8);
	std::vector<uint8_t> codewords(size_t(dataCodewords), 0);
	for (size_t i = 0; i < bits.size(); ++i) codewords[i >> 3] |= uint8_t(bits[i] << (7 - (i & 7)));
	std::vector<uint8_t> ecc = reedSolomon(codewords, eccCodewords[version - 1]);
	codewords.insert(codewords.end(), ecc.begin(), ecc.end());

	int size = 17 + 4 * version;
	Grid g{size, std::vector<std::vector<bool>>(size, std::vector<bool>(size)),
	       std::vector<std::vector<bool>>(size, std::vector<bool>(size))};
	for (int i = 0; i < size; ++i) {
		g.set(6, i, i % 2 == 0);
		g.set(i, 6, i % 2 == 0);
	}
	finder(g, 3, 3);
	finder(g, size - 4, 3);
	finder(g, 3, size - 4);
	if (version > 1) alignment(g, size - 7, size - 7);
	drawFormat(g, 0);

	size_t bit = 0;
	for (int right = size - 1; right >= 1; right -= 2) {
		if (right == 6) right = 5;
		for (int vert = 0; vert < size; ++vert) {
			for (int j = 0; j < 2; ++j) {
				int x = right - j;
				int y = ((right + 1) & 2) == 0 ? size - 1 - vert : vert;
				if (!g.function[y][x] && bit < codewords.size() * 8) {
					g.dark[y][x] = (codewords[bit >> 3] >> (7 - (bit & 7))) & 1;
					++bit;
				}
			}
		}
	}

	std::vector<std::vector<bool>> best;
	int bestPenalty = 0;
	for (int mask = 0; mask < 8; ++mask) {
		Grid t = g;
		for (int y = 0; y < size; ++y)
			for (int x = 0; x < size; ++x)
				if (!t.function[y][x] && maskBit(mask, x, y)) t.dark[y][x] = !t.dark[y][x];
		drawFormat(t, mask);
		int p = penalty(t.dark);
		if (best.empty() || p < bestPenalty) {
			best = t.dark;
			bestPenalty = p;
		}
	}
	return best;
}
