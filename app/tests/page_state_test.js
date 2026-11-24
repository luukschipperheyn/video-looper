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
	const statePath = path.join(web, "page_state.js");
	if (html.includes("/*PAGE_STATE*/") && fs.existsSync(statePath)) {
		const source = fs.readFileSync(statePath, "utf8");
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

process.exit(failed === 0 ? 0 : 1);
