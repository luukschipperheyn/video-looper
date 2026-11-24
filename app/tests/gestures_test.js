"use strict";

const g = require("../web/gestures.js");

let failed = 0;

function check(name, ok) {
	console.log(ok ? "PASS" : "FAIL", name);
	if (!ok) failed += 1;
}

const wide = { minX: -4, maxX: 4, minY: -4, maxY: 4, minS: 0.05, maxS: 8, minR: -100, maxR: 100 };

check(
	"opacity quarter track",
	g.relativeDrag({ value: 0.5, deltaPx: 50, trackPx: 200, min: 0, max: 1 }) === 0.75,
);
check("zero track keeps value", g.relativeDrag({ value: 0.5, deltaPx: 50, trackPx: 0, min: 0, max: 1 }) === 0.5);
check(
	"relative drag clamps high",
	g.relativeDrag({ value: 0.9, deltaPx: 200, trackPx: 200, min: 0, max: 1 }) === 1,
);
check(
	"relative drag clamps low",
	g.relativeDrag({ value: 0.1, deltaPx: -200, trackPx: 200, min: 0, max: 1 }) === 0,
);

const inside = g.doubleTap({ nowMs: 1279, lastStampMs: 1000, windowMs: 280 });
check("second tap inside 280 ms fires", inside.fired === true && inside.stamp === 0);
const later = g.doubleTap({ nowMs: 1280, lastStampMs: 1000, windowMs: 280 });
check("tap at 280 ms does not fire", later.fired === false && later.stamp === 1280);
const after = g.doubleTap({ nowMs: 1500, lastStampMs: 1000, windowMs: 280 });
check("tap after 280 ms does not fire", after.fired === false && after.stamp === 1500);
const first = g.doubleTap({ nowMs: 400, lastStampMs: 0, windowMs: 280 });
check("first tap arms the stamp", first.fired === false && first.stamp === 400);
const chained = g.doubleTap({ nowMs: 1600, lastStampMs: inside.stamp, windowMs: 280 });
check("fired tap resets the stamp", chained.fired === false && chained.stamp === 1600);

const swallowed = g.loopRangeDrag({ part: "start", start: 0.5, length: 0.25, dx: 1000, width: 100 });
check("loop start keeps the end and 0.02", swallowed.start === 0.73 && swallowed.length === 0.75 - 0.73);
const endLow = g.loopRangeDrag({ part: "end", start: 0.2, length: 0.5, dx: -1000, width: 100 });
check("loop end clamps length to 0.02", endLow.start === 0.2 && endLow.length === 0.02);
const endHigh = g.loopRangeDrag({ part: "end", start: 0.2, length: 0.5, dx: 1000, width: 100 });
check("loop end clamps length to 1 - start", endHigh.start === 0.2 && endHigh.length === 0.8);
const body = g.loopRangeDrag({ part: "body", start: 0.1, length: 0.5, dx: 1000, width: 100 });
check("loop body keeps length inside the cap", body.start === 0.5 && body.length === 0.5);
const bodyCap = g.loopRangeDrag({ part: "body", start: 0, length: 0.0001, dx: 10000, width: 100 });
check("loop body caps start at 0.999", bodyCap.start === 0.999 && bodyCap.length === 0.0001);
const noWidth = g.loopRangeDrag({ part: "body", start: 0.2, length: 0.3, dx: 40, width: 0 });
check("loop drag with no width keeps the inputs", noWidth.start === 0.2 && noWidth.length === 0.3);

const moved = g.oneFingerMove({
	tx: 0,
	ty: 1,
	dx: 50,
	dy: 25,
	frameWidth: 100,
	frameHeight: 50,
	minX: -4,
	maxX: 4,
	minY: -4,
	maxY: 4,
});
check("one finger uses the frame and lowers ty", moved.tx === 0.5 && moved.ty === 0.5);
const noFrame = g.oneFingerMove({
	tx: 0.25,
	ty: -0.5,
	dx: 10,
	dy: 10,
	frameWidth: 0,
	frameHeight: 80,
	minX: -4,
	maxX: 4,
	minY: -4,
	maxY: 4,
});
check("one finger with no frame keeps the inputs", noFrame.tx === 0.25 && noFrame.ty === -0.5);

const current = { tx: 1, ty: 2, sx: 3, sy: 4, rot: 5 };
const next = { tx: 9, ty: 8, sx: 7, sy: 6, rot: 0.5 };
check("shader keeps the current transform", g.applyTransform(0, current, next) === current);
check("black keeps the current transform", g.applyTransform(2, current, next) === current);
check("video takes the next transform", g.applyTransform(1, current, next) === next);
check("webcam takes the next transform", g.applyTransform(3, current, next) === next);
check("shader ignores transform", g.sourceIgnoresTransform(0) === true && g.sourceIgnoresTransform(2) === true);
check("draw ignores transform", g.sourceIgnoresTransform(4) === true);
check("video and webcam transform", g.sourceIgnoresTransform(1) === false && g.sourceIgnoresTransform(3) === false);

const aspect = g.pinch(
	Object.assign(
		{
			tx: 0,
			ty: 0,
			sx: 2,
			sy: 9,
			rot: 0,
			ax0: 0,
			ay0: 0,
			bx0: 100,
			by0: 0,
			ax1: -50,
			ay1: 0,
			bx1: 150,
			by1: 0,
			frameWidth: 200,
			frameHeight: 100,
			preserveAspect: true,
		},
		wide,
	),
);
check("pinch preserve aspect scales both axes from sx", aspect.sx === 4 && aspect.sy === 4 && aspect.tx === 0 && aspect.rot === 0);

const tiny = g.pinch(
	Object.assign(
		{
			tx: 0,
			ty: 0,
			sx: 1.5,
			sy: 1.25,
			rot: 0,
			ax0: 0,
			ay0: 0,
			bx0: 10,
			by0: 0,
			ax1: 0,
			ay1: 0,
			bx1: 12,
			by1: 0,
			frameWidth: 100,
			frameHeight: 100,
			preserveAspect: false,
		},
		wide,
	),
);
check("pinch span under 16 does not change that axis", tiny.sx === 1.5 && tiny.sy === 1.25 && tiny.tx === 0.01);

const grown = g.pinch(
	Object.assign(
		{
			tx: 0,
			ty: 0,
			sx: 1,
			sy: 3,
			rot: 0,
			ax0: 0,
			ay0: 0,
			bx0: 10,
			by0: 0,
			ax1: 0,
			ay1: 0,
			bx1: 32,
			by1: 0,
			frameWidth: 110,
			frameHeight: 100,
			preserveAspect: false,
		},
		wide,
	),
);
check("pinch span growing past 16 uses 16 as the floor", grown.sx === 2 && grown.sy === 3 && grown.tx === 0.1);

const quarter = g.pinch(
	Object.assign(
		{
			tx: 0,
			ty: 0,
			sx: 1,
			sy: 4,
			rot: 0,
			ax0: 0,
			ay0: 0,
			bx0: 100,
			by0: 0,
			ax1: 0,
			ay1: 0,
			bx1: 0,
			by1: 100,
			frameWidth: 100,
			frameHeight: 100,
			preserveAspect: true,
		},
		wide,
	),
);
check(
	"pinch quarter turn wraps into the rotation",
	quarter.sx === 1 && quarter.sy === 1 && quarter.tx === -0.5 && quarter.ty === -0.5 && quarter.rot === -Math.PI / 2,
);

const wrapped = g.pinch(
	Object.assign(
		{
			tx: 0,
			ty: 0,
			sx: 1,
			sy: 1,
			rot: 0,
			ax0: 0,
			ay0: 0,
			bx0: -100,
			by0: 0,
			ax1: 0,
			ay1: 0,
			bx1: 0,
			by1: -100,
			frameWidth: 100,
			frameHeight: 100,
			preserveAspect: true,
		},
		wide,
	),
);
check("pinch rotation wraps past half a turn", wrapped.rot === -Math.PI / 2 && wrapped.sx === 1 && wrapped.sy === 1);

const attack = g.adsrDrag({
	handle: "attack",
	attack: 0,
	decay: 1,
	sustain: 0.5,
	release: 2,
	originPx: 0,
	dx: 50,
	dy: 40,
	seg: 100,
	sustainSpan: 80,
	maxTime: 5,
});
check("attack handle moves only attack", attack.attack === 1.25 && attack.decay === 1 && attack.sustain === 0.5 && attack.release === 2);

const middle = g.adsrDrag({
	handle: "middle",
	attack: 0.2,
	decay: 0,
	sustain: 0.4,
	release: 3,
	originPx: 0,
	dx: 100,
	dy: 20,
	seg: 100,
	sustainSpan: 50,
	maxTime: 5,
});
check(
	"middle handle moves decay and sustain",
	middle.attack === 0.2 && middle.decay === 5 && middle.sustain === 0 && middle.release === 3,
);

const release = g.adsrDrag({
	handle: "release",
	attack: 0.2,
	decay: 1,
	sustain: 0.4,
	release: 3,
	originPx: 0,
	dx: 0,
	dy: 30,
	seg: 100,
	sustainSpan: 50,
	maxTime: 5,
});
check("release handle moves only release", release.attack === 0.2 && release.decay === 1 && release.sustain === 0.4 && release.release === 0);

const noSeg = g.adsrDrag({
	handle: "middle",
	attack: 0.2,
	decay: 1,
	sustain: 0.4,
	release: 3,
	originPx: 10,
	dx: 20,
	dy: -10,
	seg: 0,
	sustainSpan: 40,
	maxTime: 5,
});
check("adsr with no segment keeps the four values", noSeg.attack === 0.2 && noSeg.decay === 1 && noSeg.sustain === 0.4 && noSeg.release === 3);

check(
	"blend cells keep index family and label",
	g.blendCells.map((cell) => cell.index + " " + cell.family + " " + cell.label).join("|") ===
		"0 none Mix|1 light Add|3 light Screen|6 light Lighten|10 light Dodge|5 difference Difference|2 dark Multiply|7 dark Darken|11 dark Burn|4 contrast Overlay|9 contrast Soft Light|8 contrast Hard Light",
);
check(
	"blend hit returns the difference index",
	g.blendAtPoint({
		x: 15,
		y: 5,
		rects: [
			{ index: 3, left: 0, top: 0, right: 10, bottom: 10 },
			{ index: 5, left: 10, top: 0, right: 20, bottom: 10 },
		],
	}) === 5,
);
check(
	"blend hit misses the right and bottom edges",
	g.blendAtPoint({
		x: 20,
		y: 5,
		rects: [{ index: 5, left: 10, top: 0, right: 20, bottom: 10 }],
	}) === null &&
		g.blendAtPoint({
			x: 15,
			y: 10,
			rects: [{ index: 5, left: 10, top: 0, right: 20, bottom: 10 }],
		}) === null,
);

check(
	"short name drops a stock prefix and a repeated pair",
	g.shortName("openvid-1m_pixabay_natural_wonders_natural_wonders_6246874_002.mp4") === "natural wonder…6874 002",
);
check("short name shortens a hex token", g.shortName("take_0123456789abcdef.mp4") === "012345");
check("short name collapses a repeated word", g.shortName("session_foo_foo.mp4") === "foo");
check("short name collapses a repeated pair", g.shortName("session_alpha_beta_alpha_beta.mp4") === "alpha beta");
check(
	"short name keeps the ends of a long name",
	g.shortName("clip_abcdefghijklmnopqrstuvwxyz.mp4") === "abcdefghijklmn…stuvwxyz",
);

check("swipe ignores a short move", g.videoSwipe({ dx: 40, dy: 0, threshold: 50 }) === 0);
check("swipe ignores a vertical move", g.videoSwipe({ dx: -80, dy: 100, threshold: 50 }) === 0);
check("swipe ignores a tie", g.videoSwipe({ dx: -80, dy: 80, threshold: 50 }) === 0);
check("swipe left is the next page", g.videoSwipe({ dx: -80, dy: 10, threshold: 50 }) === 1);
check("swipe right is the previous page", g.videoSwipe({ dx: 80, dy: 0, threshold: 50 }) === -1);
check("swipe at the threshold stays", g.videoSwipe({ dx: -50, dy: 0, threshold: 50 }) === 0);

const id = g.identityTransform();
check("identity transform", id.tx === 0 && id.ty === 0 && id.sx === 1 && id.sy === 1 && id.rot === 0);
check("bpm drag up increases bpm", g.bpmDrag({ bpm: 120, dy: -30, min: 40, max: 240 }) === 130);
check("bpm drag down decreases bpm", g.bpmDrag({ bpm: 120, dy: 30, min: 40, max: 240 }) === 110);
check("bpm drag clamps", g.bpmDrag({ bpm: 40, dy: 90, min: 40, max: 240 }) === 40);

process.exit(failed === 0 ? 0 : 1);
