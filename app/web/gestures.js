(function (root, factory) {
	var api = factory();
	if (typeof module === "object" && module.exports) module.exports = api;
	root.gestures = api;
})(typeof globalThis === "object" ? globalThis : this, function () {
	"use strict";

	function clamp(v, min, max) {
		return Math.min(max, Math.max(min, v));
	}

	function wrapHalf(delta) {
		if (delta > Math.PI) delta -= Math.PI * 2;
		if (delta < -Math.PI) delta += Math.PI * 2;
		return delta;
	}

	function relativeDrag(o) {
		if (o.trackPx <= 0) return o.value;
		return clamp(o.value + (o.deltaPx / o.trackPx) * (o.max - o.min), o.min, o.max);
	}

	function doubleTap(o) {
		var fired = o.lastStampMs !== 0 && o.nowMs - o.lastStampMs < o.windowMs;
		return { fired: fired, stamp: fired ? 0 : o.nowMs };
	}

	function loopRangeDrag(o) {
		if (o.width <= 0) return { start: o.start, length: o.length };
		var end = o.start + o.length;
		var delta = o.dx / o.width;
		if (o.part === "start") {
			var start = clamp(o.start + delta, 0, end - 0.02);
			return { start: start, length: end - start };
		}
		if (o.part === "end") return { start: o.start, length: clamp(o.length + delta, 0.02, 1 - o.start) };
		if (o.part === "body") return { start: clamp(o.start + delta, 0, Math.min(0.999, 1 - o.length)), length: o.length };
		return { start: o.start, length: o.length };
	}

	function sourceIgnoresTransform(kind) {
		return kind === 0 || kind === 2 || kind === 4;
	}

	function applyTransform(kind, current, next) {
		return sourceIgnoresTransform(kind) ? current : next;
	}

	function oneFingerMove(o) {
		if (o.frameWidth <= 0 || o.frameHeight <= 0) return { tx: o.tx, ty: o.ty };
		return {
			tx: clamp(o.tx + o.dx / o.frameWidth, o.minX, o.maxX),
			ty: clamp(o.ty - o.dy / o.frameHeight, o.minY, o.maxY),
		};
	}

	function spanFactor(oldSpan, nextSpan) {
		if (oldSpan < 16 && nextSpan < 16) return 1;
		return nextSpan / Math.max(oldSpan, 16);
	}

	function pinch(o) {
		var tx = o.tx;
		var ty = o.ty;
		if (o.frameWidth > 0 && o.frameHeight > 0) {
			var midDx = (o.ax1 + o.bx1) / 2 - (o.ax0 + o.bx0) / 2;
			var midDy = (o.ay1 + o.by1) / 2 - (o.ay0 + o.by0) / 2;
			tx = clamp(o.tx + midDx / o.frameWidth, o.minX, o.maxX);
			ty = clamp(o.ty - midDy / o.frameHeight, o.minY, o.maxY);
		}
		var sx = o.sx;
		var sy = o.sy;
		if (o.preserveAspect) {
			var oldD = Math.hypot(o.bx0 - o.ax0, o.by0 - o.ay0);
			var newD = Math.hypot(o.bx1 - o.ax1, o.by1 - o.ay1);
			if (oldD > 1) {
				sx = o.sx * (newD / oldD);
				sy = sx;
			}
		} else {
			sx = o.sx * spanFactor(Math.abs(o.bx0 - o.ax0), Math.abs(o.bx1 - o.ax1));
			sy = o.sy * spanFactor(Math.abs(o.by0 - o.ay0), Math.abs(o.by1 - o.ay1));
		}
		var delta = wrapHalf(Math.atan2(o.by1 - o.ay1, o.bx1 - o.ax1) - Math.atan2(o.by0 - o.ay0, o.bx0 - o.ax0));
		return {
			tx: tx,
			ty: ty,
			sx: clamp(sx, o.minS, o.maxS),
			sy: clamp(sy, o.minS, o.maxS),
			rot: clamp(o.rot - delta, o.minR, o.maxR),
		};
	}

	function timeFromPx(px, seg, maxTime) {
		var u = clamp(px / seg, 0, 1);
		return maxTime * u * u;
	}

	function adsrDrag(o) {
		var out = { attack: o.attack, decay: o.decay, sustain: o.sustain, release: o.release };
		if (o.seg <= 0) return out;
		if (o.handle === "attack") {
			out.attack = timeFromPx(o.originPx + o.dx, o.seg, o.maxTime);
			return out;
		}
		if (o.handle === "release") {
			out.release = timeFromPx(o.originPx + o.dx, o.seg, o.maxTime);
			return out;
		}
		if (o.handle === "middle") {
			out.decay = timeFromPx(o.originPx + o.dx, o.seg, o.maxTime);
			if (o.sustainSpan > 0) out.sustain = clamp(o.sustain - o.dy / o.sustainSpan, 0, 1);
			return out;
		}
		return out;
	}

	var blendCells = [
		{ index: 0, family: "none", label: "Mix" },
		{ index: 1, family: "light", label: "Add" },
		{ index: 3, family: "light", label: "Screen" },
		{ index: 6, family: "light", label: "Lighten" },
		{ index: 10, family: "light", label: "Dodge" },
		{ index: 5, family: "difference", label: "Difference" },
		{ index: 2, family: "dark", label: "Multiply" },
		{ index: 7, family: "dark", label: "Darken" },
		{ index: 11, family: "dark", label: "Burn" },
		{ index: 4, family: "contrast", label: "Overlay" },
		{ index: 9, family: "contrast", label: "Soft Light" },
		{ index: 8, family: "contrast", label: "Hard Light" },
	];

	function blendAtPoint(o) {
		var rects = o.rects;
		for (var i = 0; i < rects.length; i++) {
			var r = rects[i];
			if (r.left <= o.x && o.x < r.right && r.top <= o.y && o.y < r.bottom) return r.index;
		}
		return null;
	}

	function shortName(filename) {
		var s = String(filename).replace(/\.(mp4|mov|m4v|mkv|avi)$/i, "");
		var cut = s.indexOf("_");
		if (cut >= 0) s = s.slice(cut + 1);
		s = s.replace(/_/g, " ").replace(/\b(pixabay|pexels|videos)\b/g, "");
		var prev;
		do {
			prev = s;
			s = s.replace(/\b(\w+ \w+) \1\b/g, "$1").replace(/\b(\w+) \1\b/g, "$1");
		} while (s !== prev);
		s = s.replace(/\b[0-9a-fA-F]{10,}\b/g, function (token) {
			return token.slice(0, 6);
		});
		s = s.replace(/\s+/g, " ").trim();
		if (s.length > 24) s = s.slice(0, 14) + "…" + s.slice(-8);
		return s;
	}

	function bpmDrag(o) {
		return clamp(Math.round(o.bpm - o.dy / 3), o.min, o.max);
	}

	function identityTransform() {
		return { tx: 0, ty: 0, sx: 1, sy: 1, rot: 0 };
	}

	function videoSwipe(o) {
		if (Math.abs(o.dx) > o.threshold && Math.abs(o.dx) > Math.abs(o.dy)) return o.dx < 0 ? 1 : -1;
		return 0;
	}

	return {
		relativeDrag: relativeDrag,
		doubleTap: doubleTap,
		loopRangeDrag: loopRangeDrag,
		sourceIgnoresTransform: sourceIgnoresTransform,
		applyTransform: applyTransform,
		oneFingerMove: oneFingerMove,
		pinch: pinch,
		adsrDrag: adsrDrag,
		blendCells: blendCells,
		blendAtPoint: blendAtPoint,
		shortName: shortName,
		bpmDrag: bpmDrag,
		identityTransform: identityTransform,
		videoSwipe: videoSwipe,
	};
});
