"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

let failed = 0;

function check(name, ok) {
	console.log(ok ? "PASS" : "FAIL", name);
	if (!ok) failed += 1;
}

function num(def, min, max) {
	return { type: "float", min: min, max: max, def: def, writable: true };
}

function integer(def, min, max) {
	return { type: "int", min: min, max: max, def: def, writable: true };
}

const schema = {
	record_blend_opacity: num(1, 0, 1),
	fx_param1: num(0.5, 0, 1),
	fx_param2: num(0.5, 0, 1),
	fx_param3: num(0, -1, 1),
	fx_count: num(0, 0, 16),
	source_fx_param1: num(0.5, 0, 1),
	source_fx_param2: num(0.5, 0, 1),
	source_fx_param3: num(0, -1, 1),
	source_fx_count: num(0, 0, 16),
	read_speed_mult: num(1, 0, 4),
	gain_mix: num(1, 0, 2),
	env_attack: num(0, 0, 4),
	env_decay: num(0, 0, 4),
	env_sustain: num(1, 0, 1),
	env_release: num(0, 0, 8),
	record_blend_mode: integer(0, 0, 11),
	fx_repeat_blend: integer(6, 0, 11),
	source_fx_repeat_blend: integer(6, 0, 11),
};

function createEl(tag) {
	const classes = new Set();
	const children = [];
	const childNodes = [];
	let className = "";
	let textOverride = null;
	const el = {
		nodeType: 1,
		tagName: String(tag || "div").toUpperCase(),
		children: children,
		childNodes: childNodes,
		dataset: {},
		style: { setProperty(name, value) { this[name] = value; } },
		parent: null,
		clientWidth: 0,
		clientHeight: 0,
		hidden: false,
		isConnected: true,
		_listeners: {},
		_rect: { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 },
	};
	function syncClass() {
		className = [...classes].join(" ");
	}
	el.classList = {
		contains(name) { return classes.has(name); },
		add() {
			for (const name of arguments) if (name) classes.add(name);
			syncClass();
		},
		remove() {
			for (const name of arguments) classes.delete(name);
			syncClass();
		},
		toggle(name, force) {
			const on = force === undefined ? !classes.has(name) : !!force;
			if (on) classes.add(name);
			else classes.delete(name);
			syncClass();
			return on;
		},
	};
	Object.defineProperty(el, "className", {
		get() { return className; },
		set(value) {
			classes.clear();
			className = String(value || "");
			for (const part of className.split(/\s+/)) if (part) classes.add(part);
		},
	});
	Object.defineProperty(el, "textContent", {
		get() {
			if (textOverride != null) return textOverride;
			return childNodes.map(function (node) {
				return node.nodeType === 3 ? node.textContent : node.textContent;
			}).join("");
		},
		set(value) {
			textOverride = String(value);
			children.length = 0;
			childNodes.length = 0;
		},
	});
	el.setAttribute = function (name, value) {
		if (name === "class") el.className = value;
		else el[name] = value;
	};
	el.getAttribute = function (name) { return el[name]; };
	el.removeAttribute = function (name) { delete el[name]; };
	el.addEventListener = function (type, fn) {
		if (!el._listeners[type]) el._listeners[type] = [];
		el._listeners[type].push(fn);
	};
	el.setPointerCapture = function () {};
	el.getBoundingClientRect = function () { return el._rect; };
	el.getContext = function () {
		return {
			clearRect() {}, beginPath() {}, arc() {}, fill() {}, moveTo() {}, lineTo() {}, stroke() {},
			drawImage() {}, fillRect() {},
		};
	};
	el.toBlob = function (cb) { if (cb) cb(null); };
	el.append = function () {
		for (const node of arguments) {
			if (node == null || node === false) continue;
			const child = node.nodeType ? node : { nodeType: 3, textContent: String(node) };
			child.parent = el;
			childNodes.push(child);
			if (child.nodeType === 1) children.push(child);
			textOverride = null;
		}
	};
	el.replaceChildren = function () {
		children.length = 0;
		childNodes.length = 0;
		textOverride = null;
		el.append.apply(el, arguments);
	};
	el.querySelectorAll = function (selector) {
		const steps = selector.trim().split(/\s+/);
		let level = [el];
		for (const step of steps) {
			const next = [];
			for (const node of level) collect(node, step, next);
			level = next;
		}
		return level;
	};
	el.querySelector = function (selector) {
		const found = el.querySelectorAll(selector);
		return found.length ? found[0] : null;
	};
	return el;
}

function matches(el, sel) {
	const parts = sel.split(".").filter(Boolean);
	let classAt = 0;
	if (!sel.startsWith(".")) {
		if (String(el.tagName).toLowerCase() !== parts[0].toLowerCase()) return false;
		classAt = 1;
	}
	for (let i = classAt; i < parts.length; i++) {
		if (!el.classList.contains(parts[i])) return false;
	}
	return true;
}

function collect(el, sel, out) {
	for (const kid of el.children) {
		if (matches(kid, sel)) out.push(kid);
		collect(kid, sel, out);
	}
}

function pageHtml() {
	const web = path.join(__dirname, "../web");
	let html = fs.readFileSync(path.join(web, "index.html"), "utf8");
	const gestures = fs.readFileSync(path.join(web, "gestures.js"), "utf8");
	html = html.replace("/*GESTURES*/", function () { return gestures; });
	if (html.includes("/*PAGE_STATE*/")) {
		const source = fs.readFileSync(path.join(web, "page_state.js"), "utf8");
		html = html.replace("/*PAGE_STATE*/", function () { return source; });
	}
	html = html.replace("/*CONTROL_SCHEMA*/null", function () { return JSON.stringify(schema); });
	return html;
}

function boot() {
	const viewport = createEl("div");
	const holder = createEl("div");
	const hands = createEl("div");
	viewport.append(holder);
	holder.append(hands);
	const ids = { viewport: viewport, holder: holder, "two-hands": hands };
	const sockets = [];
	function WebSocket() {
		this.readyState = 0;
		this.send = function () {};
		sockets.push(this);
	}
	WebSocket.CONNECTING = 0;
	WebSocket.OPEN = 1;
	WebSocket.CLOSING = 2;
	WebSocket.CLOSED = 3;
	const store = new Map();
	const context = {
		console: console,
		setTimeout: setTimeout,
		clearTimeout: clearTimeout,
		setInterval: setInterval,
		clearInterval: clearInterval,
		performance: performance,
		URLSearchParams: URLSearchParams,
		requestAnimationFrame: function () {},
		WebSocket: WebSocket,
		localStorage: {
			getItem(key) { return store.has(key) ? store.get(key) : null; },
			setItem(key, value) { store.set(key, String(value)); },
		},
		ResizeObserver: function () {
			this.observe = function () {};
			this.disconnect = function () {};
		},
		location: { search: "", host: "looper.local" },
		window: {
			addEventListener() {},
			matchMedia() { return { matches: false }; },
		},
		document: {
			createElement: createEl,
			createElementNS: function (_ns, tag) { return createEl(tag); },
			createTextNode: function (text) { return { nodeType: 3, textContent: String(text) }; },
			getElementById: function (id) { return ids[id] || null; },
			addEventListener() {},
		},
	};
	vm.createContext(context);
	context.globalThis = context;
	const html = pageHtml();
	const re = /<script>([\s\S]*?)<\/script>/g;
	let match;
	while ((match = re.exec(html))) vm.runInContext(match[1], context, { filename: "index.html" });
	return { hands: hands, socket: sockets[0] };
}

function painted(grid) {
	const on = [];
	for (const button of grid.children) {
		if (button.classList.contains("on")) on.push(Number(button.dataset.index));
	}
	return on.length === 1 ? on[0] : null;
}

function press(grid, index) {
	for (const button of grid.children) {
		button._rect = Number(button.dataset.index) === index
			? { left: 0, top: 0, right: 40, bottom: 40, width: 40, height: 40 }
			: { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 };
	}
	const event = {
		pointerId: 1,
		clientX: 10,
		clientY: 10,
		button: 0,
		pointerType: "touch",
		preventDefault() {},
		stopPropagation() {},
		target: grid,
	};
	for (const fn of grid._listeners.pointerdown || []) fn(event);
}

const page = boot();
const socket = page.socket;
socket.readyState = 1;
socket.onopen();
const overdub = page.hands.querySelector(".panel.blend .bgrid");
const picks = page.hands.querySelectorAll(".fxedit .blendpick .bgrid");
if (!overdub || picks.length !== 2) {
	console.log("FAIL blend grids were not on the page", overdub ? 1 : 0, picks.length);
	process.exit(2);
}
press(overdub, 2);
press(picks[0], 4);
press(picks[1], 8);
if (painted(overdub) !== 2 || painted(picks[0]) !== 4 || painted(picks[1]) !== 8) {
	console.log(
		"FAIL finger did not set the blend",
		painted(overdub),
		painted(picks[0]),
		painted(picks[1]),
	);
	process.exit(2);
}
socket.onmessage({
	data: JSON.stringify({
		t: "snapshot",
		record_blend_mode: 9,
		fx_repeat_blend: 9,
		source_fx_repeat_blend: 9,
	}),
});
check("frame while the Overdub blend grid is held keeps the finger value", painted(overdub) === 2);
check("frame while the FX blend picker is held keeps the finger value", painted(picks[0]) === 4);
check("frame while the Source FX blend picker is held keeps the finger value", painted(picks[1]) === 8);

const pageState = require("../web/page_state.js");

const controlSchema = {
	record_blend_opacity: { type: "float", min: 0, max: 1, def: 1 },
	fx_repeat_blend: { type: "int", min: 0, max: 11, def: 6 },
	source_fx_repeat_blend: { type: "int", min: 0, max: 11, def: 6 },
	bpm: { type: "float", min: 40, max: 240, def: 120 },
	record_enabled: { type: "bool", min: 0, max: 1, def: false },
};

function fakeTimers() {
	let now = 0;
	let next = 1;
	const intervals = [];
	return {
		now() { return now; },
		setInterval(fn, ms) {
			const id = next++;
			intervals.push({ id: id, fn: fn, ms: ms, left: ms });
			return id;
		},
		clearInterval(id) {
			const at = intervals.findIndex((item) => item.id === id);
			if (at >= 0) intervals.splice(at, 1);
		},
		advance(ms) {
			let left = ms;
			while (left > 0) {
				if (!intervals.length) {
					now += left;
					return;
				}
				let step = left;
				for (const item of intervals) step = Math.min(step, item.left);
				now += step;
				left -= step;
				for (const item of intervals.slice()) {
					item.left -= step;
					if (item.left <= 0) {
						item.left = item.ms;
						item.fn();
					}
				}
			}
		},
	};
}

function harness() {
	const sent = [];
	const socket = {
		readyState: 1,
		send(text) { sent.push(JSON.parse(text)); },
	};
	const timers = fakeTimers();
	const model = pageState.create(controlSchema, socket, timers);
	model.linkUp();
	return { model: model, sent: sent, timers: timers, socket: socket };
}

function named(sent, type, name) {
	return sent.filter((msg) => msg.t === type && msg.name === name);
}

{
	const { model, sent } = harness();
	model.fingerDown(["fx_repeat_blend", "source_fx_repeat_blend"]);
	model.value("fx_repeat_blend", 4);
	model.value("source_fx_repeat_blend", 8);
	model.value("bpm", 90);
	const view = model.frame({
		t: "snapshot",
		fx_repeat_blend: 9,
		source_fx_repeat_blend: 9,
		bpm: 120,
		record_blend_opacity: 0.3,
	});
	check(
		"a frame keeps every locked control and writes the rest",
		view.local.fx_repeat_blend === 4 &&
			view.local.source_fx_repeat_blend === 8 &&
			view.local.bpm === 120 &&
			view.local.record_blend_opacity === 0.3,
	);
	const lifted = model.fingerUp(["fx_repeat_blend", "source_fx_repeat_blend"]);
	check(
		"finger up restores the frame value",
		lifted.local.fx_repeat_blend === 9 && lifted.local.source_fx_repeat_blend === 9,
	);
	check(
		"finger up records the value under the finger",
		sent.some((msg) => msg.t === "use" && msg.e === "set" && msg.name === "fx_repeat_blend" && msg.v === 4) &&
			sent.some((msg) => msg.t === "use" && msg.e === "set" && msg.name === "source_fx_repeat_blend" && msg.v === 8),
	);
}

{
	const { model } = harness();
	model.frame({ t: "snapshot", record_blend_opacity: 0.2 });
	model.value("record_blend_opacity", 0.8);
	const rolled = model.reject({ t: "reject", name: "record_blend_opacity" });
	check("reject restores the last frame value", rolled.local.record_blend_opacity === 0.2);
	model.fingerDown(["record_blend_opacity"]);
	model.value("record_blend_opacity", 0.8);
	const held = model.reject({ t: "reject", name: "record_blend_opacity" });
	check("reject keeps the value under the finger", held.local.record_blend_opacity === 0.8);
}

{
	const { model, sent, timers } = harness();
	model.frame({ t: "snapshot", bpm: 100 });
	model.fingerDown(["bpm"]);
	model.value("bpm", 140);
	model.holdDown("gate");
	const before = sent.length;
	const lost = model.linkLost();
	timers.advance(500);
	check(
		"link loss drops the lock, restores the frame, and stops holds",
		lost.link === "down" &&
			lost.local.bpm === 100 &&
			lost.fingers.gate === false &&
			!model.locked("bpm") &&
			sent.length === before &&
			named(sent, "release", "gate").length === 0,
	);
}

{
	const { model, sent, timers } = harness();
	model.holdDown("gate");
	const started = named(sent, "hold", "gate").length;
	timers.advance(99);
	check("a hold waits 100 ms before the next keep-alive", named(sent, "hold", "gate").length === started);
	timers.advance(1);
	check("a hold repeats when 100 ms elapse", named(sent, "hold", "gate").length === started + 1);
	timers.advance(200);
	check("a hold repeats on each 100 ms", named(sent, "hold", "gate").length === started + 3);
	model.holdUp("gate");
	const afterUp = named(sent, "hold", "gate").length;
	timers.advance(500);
	check(
		"finger up sends release and stops the keep-alive",
		afterUp === started + 3 &&
			named(sent, "hold", "gate").length === afterUp &&
			named(sent, "release", "gate").length === 1,
	);
}

{
	const { model, sent, timers } = harness();
	model.holdDown("gate");
	model.holdDown("erase");
	model.holdDown("cue");
	const before = sent.length;
	model.panic();
	timers.advance(500);
	const added = sent.slice(before);
	check(
		"Panic stops the holds without a release message",
		added.length === 5 &&
			added[0].t === "panic" &&
			added[1].t === "use" && added[1].e === "release" && added[1].name === "gate" &&
			added[2].t === "use" && added[2].e === "release" && added[2].name === "erase" &&
			added[3].t === "use" && added[3].e === "release" && added[3].name === "cue" &&
			added[4].t === "use" && added[4].e === "act" && added[4].name === "panic" &&
			named(sent, "release", "gate").length === 0 &&
			named(sent, "hold", "gate").length === 1 &&
			model.state.fingers.gate === false &&
			model.state.fingers.erase === false &&
			model.state.fingers.cue === false,
	);
}

{
	const host = {};
	const base = fakeTimers();
	const timers = {
		host: host,
		now: base.now,
		advance: base.advance,
		setInterval(fn, ms) {
			if (this !== host) throw new TypeError("Can only call setInterval on a Window");
			return base.setInterval(fn, ms);
		},
		clearInterval(id) {
			if (this !== host) throw new TypeError("Can only call clearInterval on a Window");
			return base.clearInterval(id);
		},
	};
	const sent = [];
	const socket = { readyState: 1, send(text) { sent.push(JSON.parse(text)); } };
	const model = pageState.create(controlSchema, socket, timers);
	model.linkUp();
	let threw = null;
	try {
		model.holdDown("gate");
		timers.advance(500);
	} catch (err) {
		threw = err;
	}
	const holds = named(sent, "hold", "gate").length;
	check(
		"a hold keeps the Overdub gate down past the silence window",
		threw == null && holds === 6,
	);
	try {
		model.holdUp("gate");
		timers.advance(500);
	} catch (err) {
		threw = err;
	}
	check(
		"finger up still releases that hold",
		threw == null && named(sent, "release", "gate").length === 1 && named(sent, "hold", "gate").length === holds,
	);
}

process.exit(failed === 0 ? 0 : 1);
