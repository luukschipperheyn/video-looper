#pragma once

// Destination rectangle for CSS object-fit: contain.
// The source keeps its aspect ratio, sits inside the destination, and touches
// either the full width or the full height. The other axis is left empty.
struct ContainRect {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};

inline ContainRect containRect(int srcW, int srcH, int dstW, int dstH) {
	ContainRect r;
	if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return r;

	// src is wider than dst when srcW/srcH > dstW/dstH.
	long long srcCross = static_cast<long long>(srcW) * dstH;
	long long dstCross = static_cast<long long>(dstW) * srcH;
	if (srcCross >= dstCross) {
		r.w = dstW;
		r.h = static_cast<int>((static_cast<long long>(dstW) * srcH + srcW / 2) / srcW);
	} else {
		r.h = dstH;
		r.w = static_cast<int>((static_cast<long long>(dstH) * srcW + srcH / 2) / srcH);
	}
	if (r.w < 1) r.w = 1;
	if (r.h < 1) r.h = 1;
	if (r.w > dstW) r.w = dstW;
	if (r.h > dstH) r.h = dstH;
	r.x = (dstW - r.w) / 2;
	r.y = (dstH - r.h) / 2;
	return r;
}
