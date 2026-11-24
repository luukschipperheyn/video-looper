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

check("touch drag stays a move", g.pointerDragMode({ pointerType: "touch", button: 2, altKey: true, shiftKey: true }) === "move");
check("mouse drag moves", g.pointerDragMode({ pointerType: "mouse", button: 0, altKey: false, shiftKey: false }) === "move");
check("alt drag scales", g.pointerDragMode({ pointerType: "mouse", button: 0, altKey: true, shiftKey: false }) === "scale");
check("shift drag rotates", g.pointerDragMode({ pointerType: "mouse", button: 0, altKey: true, shiftKey: true }) === "rotate");
check("right button rotates", g.pointerDragMode({ pointerType: "mouse", button: 2, altKey: false, shiftKey: false }) === "rotate");
check("middle button is not a drag", g.pointerDragMode({ pointerType: "pen", button: 1, altKey: false, shiftKey: false }) === "");
check("pen drag moves", g.pointerDragMode({ pointerType: "pen", button: 0, altKey: false, shiftKey: false }) === "move");

const doubled = g.dragScale({
	sx: 1.5,
	sy: 9,
	dx: 0,
	dy: -50,
	frameWidth: 80,
	frameHeight: 50,
	preserveAspect: true,
	minS: 0.05,
	maxS: 8,
});
check("alt drag up one frame doubles both axes", doubled.sx === 3 && doubled.sy === 3);
const halved = g.dragScale({
	sx: 2,
	sy: 2,
	dx: 10,
	dy: 40,
	frameWidth: 80,
	frameHeight: 40,
	preserveAspect: true,
	minS: 0.05,
	maxS: 8,
});
check("alt drag down one frame halves both axes", halved.sx === 1 && halved.sy === 1);
const stretched = g.dragScale({
	sx: 1,
	sy: 4,
	dx: 40,
	dy: -20,
	frameWidth: 40,
	frameHeight: 20,
	preserveAspect: false,
	minS: 0.05,
	maxS: 8,
});
check("free alt drag widens on x and grows on y", stretched.sx === 2 && stretched.sy === 8);
const capped = g.dragScale({
	sx: 5,
	sy: 1,
	dx: 10,
	dy: -100,
	frameWidth: 10,
	frameHeight: 100,
	preserveAspect: false,
	minS: 0.05,
	maxS: 8,
});
check("alt drag clamps scale", capped.sx === 8 && capped.sy === 2);
const noScaleFrame = g.dragScale({
	sx: 1.25,
	sy: 1.5,
	dx: 10,
	dy: -10,
	frameWidth: 40,
	frameHeight: 0,
	preserveAspect: false,
	minS: 0.05,
	maxS: 8,
});
check("alt drag with no frame keeps the scale", noScaleFrame.sx === 1.25 && noScaleFrame.sy === 1.5);

check(
	"shift drag across the frame is a half turn clockwise",
	g.dragRotate({ rot: 0, dx: 200, frameWidth: 200, minR: -100, maxR: 100 }) === -Math.PI,
);
check(
	"shift drag left turns counter-clockwise",
	g.dragRotate({ rot: 0, dx: -100, frameWidth: 200, minR: -100, maxR: 100 }) === Math.PI / 2,
);
check("shift drag with no frame keeps the rotation", g.dragRotate({ rot: 0.4, dx: 30, frameWidth: 0, minR: -100, maxR: 100 }) === 0.4);
check("shift drag clamps rotation", g.dragRotate({ rot: 0, dx: -1000, frameWidth: 100, minR: -1, maxR: 1 }) === 1);

const grownScale = g.scaleByFactor({ sx: 2, sy: 3, factor: 2, preserveAspect: false, minS: 0.05, maxS: 8 });
check("pinch factor scales each axis", grownScale.sx === 4 && grownScale.sy === 6);
const lockedScale = g.scaleByFactor({ sx: 2, sy: 9, factor: 2, preserveAspect: true, minS: 0.05, maxS: 8 });
check("pinch factor with preserve aspect follows sx", lockedScale.sx === 4 && lockedScale.sy === 4);
const idleScale = g.scaleByFactor({ sx: 2, sy: 9, factor: 1, preserveAspect: true, minS: 0.05, maxS: 8 });
check("pinch factor of 1 keeps both axes", idleScale.sx === 2 && idleScale.sy === 9);
const badScale = g.scaleByFactor({ sx: 2, sy: 3, factor: 0, preserveAspect: false, minS: 0.05, maxS: 8 });
check("a pinch factor of 0 keeps both axes", badScale.sx === 2 && badScale.sy === 3);

const pinchOut = g.pinchScale({ sx: 1, sy: 2, deltaY: -100, deltaMode: 0, preserveAspect: false, minS: 0.05, maxS: 8 });
check("trackpad pinch out grows both axes", pinchOut.sx === Math.E && pinchOut.sy === 2 * Math.E);
const pinchLocked = g.pinchScale({ sx: 1, sy: 9, deltaY: -100, deltaMode: 0, preserveAspect: true, minS: 0.05, maxS: 8 });
check("trackpad pinch with preserve aspect follows sx", pinchLocked.sx === Math.E && pinchLocked.sy === Math.E);
const pinchStill = g.pinchScale({ sx: 1.5, sy: 1.25, deltaY: 0, deltaMode: 0, preserveAspect: true, minS: 0.05, maxS: 8 });
check("empty pinch keeps both axes", pinchStill.sx === 1.5 && pinchStill.sy === 1.25);
const pinchLine = g.pinchScale({ sx: 1, sy: 1, deltaY: 100 / 32, deltaMode: 1, preserveAspect: false, minS: 0.05, maxS: 8 });
const pinchPixel = g.pinchScale({ sx: 1, sy: 1, deltaY: 100, deltaMode: 0, preserveAspect: false, minS: 0.05, maxS: 8 });
check("a pinch line matches 32 pixels", pinchLine.sx === pinchPixel.sx && pinchLine.sy === pinchPixel.sy);

check("trackpad twist of a quarter turn is clockwise", g.gestureRotate({ rot: 0, degrees: 90, minR: -100, maxR: 100 }) === -Math.PI / 2);
check("trackpad twist the other way", g.gestureRotate({ rot: 0, degrees: -90, minR: -100, maxR: 100 }) === Math.PI / 2);
check("trackpad twist of zero keeps rotation", g.gestureRotate({ rot: 0.2, degrees: 0, minR: -100, maxR: 100 }) === 0.2);
check("trackpad twist clamps", g.gestureRotate({ rot: 0, degrees: -100000, minR: -1, maxR: 1 }) === 1);

const trackpad = g.gesturePinch(
	Object.assign(
		{
			tx: 0.2,
			ty: -0.1,
			sx: 2,
			sy: 3,
			rot: 0,
			x0: 10,
			y0: 20,
			x1: 110,
			y1: 70,
			frameWidth: 200,
			frameHeight: 100,
			scale: 2,
			degrees: 90,
			preserveAspect: false,
		},
		wide,
	),
);
check(
	"trackpad pinch moves by the center and scales and rotates",
	trackpad.tx === 0.7 &&
		trackpad.ty === -0.6 &&
		trackpad.sx === 4 &&
		trackpad.sy === 6 &&
		trackpad.rot === -Math.PI / 2,
);
const trackpadStill = g.gesturePinch(
	Object.assign(
		{
			tx: 0.2,
			ty: -0.1,
			sx: 2,
			sy: 9,
			rot: 0.2,
			x0: 10,
			y0: 20,
			x1: 10,
			y1: 20,
			frameWidth: 200,
			frameHeight: 100,
			scale: 1,
			degrees: 0,
			preserveAspect: true,
		},
		wide,
	),
);
check(
	"trackpad pinch with a still center keeps the position",
	trackpadStill.tx === 0.2 && trackpadStill.ty === -0.1 && trackpadStill.sx === 2 && trackpadStill.sy === 9 && trackpadStill.rot === 0.2,
);

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
