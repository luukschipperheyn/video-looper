(function (root, factory) {
	var api = factory();
	if (typeof module === "object" && module.exports) module.exports = api;
	root.pageState = api;
})(typeof globalThis === "object" ? globalThis : this, function () {
	"use strict";

	var HOLD_MS = 100;

	function create(schema, socket, timers, initial) {
		initial = initial || {};
		var state = {
			overlay: null,
			link: "connecting",
			remote: {},
			local: {},
			grabbed: new Set(),
			fingers: { gate: false, cue: false, erase: false },
			holds: { gate: 0, cue: 0, erase: 0 },
			videos: [],
			folders: [],
			page: 0,
			pageSize: 5,
			paged: false,
			tap: initial.tap || "select",
			fxChain: initial.fxChain || "program",
			followed: undefined,
			exportBase: 0,
			exportOn: false,
			exportReason: "",
			midError: "",
			midKind: "",
			relFrom: 1,
			relStage: 0,
			taps: {},
			imageAspect: 16 / 9,
		};

		function socketOpen() {
			return socket.readyState === 1;
		}

		function send(msg) {
			if (socketOpen()) socket.send(JSON.stringify(msg));
		}

		function use(event, name, value) {
			var msg = { t: "use", e: event, name: name };
			if (value !== undefined) msg.v = value;
			send(msg);
		}

		function release(names) {
			for (const name of names) {
				state.grabbed.delete(name);
				if (Object.prototype.hasOwnProperty.call(state.remote, name)) state.local[name] = state.remote[name];
			}
		}

		function noteExport(on) {
			if (on && !state.exportOn) state.exportBase = timers.now();
			if (!on) state.exportBase = 0;
			state.exportOn = !!on;
			if (on) state.exportReason = "";
		}

		function stopHold(name, notify) {
			if (!state.holds[name]) return false;
			timers.clearInterval(state.holds[name]);
			state.holds[name] = 0;
			state.fingers[name] = false;
			if (notify) {
				send({ t: "release", name: name });
				use("release", name);
			}
			return true;
		}

		function fingerDown(names) {
			if (!socketOpen()) return state;
			for (const name of names) state.grabbed.add(name);
			return state;
		}

		function value(name, raw) {
			var control = schema[name];
			if (!control) return state;
			var v = raw;
			if (control.type === "bool") v = !!raw;
			else {
				var n = Number(raw);
				if (!Number.isFinite(n)) return state;
				v = Math.min(control.max, Math.max(control.min, n));
				if (control.type === "int") v = Math.round(Math.min(control.max, Math.max(control.min, v)));
			}
			state.local[name] = v;
			send({ t: "set", name: name, value: v });
			return state;
		}

		function fingerUp(names) {
			for (const name of names) {
				if (state.grabbed.has(name)) use("set", name, state.local[name]);
			}
			release(names);
			return state;
		}

		function holdDown(name) {
			if (state.link !== "up" || !socketOpen() || state.holds[name]) return false;
			state.fingers[name] = true;
			var sendHold = function () { send({ t: "hold", name: name }); };
			sendHold();
			use("hold", name);
			state.holds[name] = timers.setInterval(sendHold, HOLD_MS);
			return true;
		}

		function holdUp(name) {
			state.fingers[name] = false;
			stopHold(name, true);
			return state;
		}

		function panic() {
			if (!socketOpen()) return state;
			send({ t: "panic" });
			if (stopHold("gate", false)) use("release", "gate");
			if (stopHold("erase", false)) use("release", "erase");
			if (stopHold("cue", false)) use("release", "cue");
			use("act", "panic");
			state.fingers.gate = false;
			state.fingers.erase = false;
			state.fingers.cue = false;
			return state;
		}

		function frame(msg) {
			var prevExport = !!state.local.record_enabled;
			for (const key of Object.keys(msg)) {
				if (key === "t" || key === "videos" || key === "folders") continue;
				state.remote[key] = msg[key];
			}
			if (Array.isArray(msg.videos)) state.videos = msg.videos.slice();
			if (Array.isArray(msg.folders)) state.folders = msg.folders.slice();
			for (const name of Object.keys(schema)) {
				if (state.grabbed.has(name)) continue;
				if (name in msg) state.local[name] = msg[name];
			}
			if ("source_error" in msg) {
				if (msg.source_error) {
					state.midError = msg.source_error;
					state.midKind = "source";
				} else if (state.midKind === "source") {
					state.midError = "";
					state.midKind = "";
				}
			}
			if (msg.video !== state.followed) {
				state.followed = msg.video;
				state.paged = false;
			}
			if (!state.paged) {
				var at = state.videos.indexOf(state.remote.video);
				if (at >= 0) state.page = Math.floor(at / Math.max(1, state.pageSize));
			}
			var stageN = Number(state.remote.stage) || 0;
			if (stageN === 4 && state.relStage !== 4) state.relFrom = Number(state.remote.envelope) || 0;
			state.relStage = stageN;
			var nowExport = !!state.local.record_enabled;
			if (nowExport !== prevExport || nowExport !== state.exportOn) noteExport(nowExport);
			else if (nowExport) state.exportReason = "";
			if (msg.export_error) state.exportReason = msg.export_error;
			return state;
		}

		function reject(msg) {
			if (msg.name && schema[msg.name] && Object.prototype.hasOwnProperty.call(state.remote, msg.name) && !state.grabbed.has(msg.name))
				state.local[msg.name] = state.remote[msg.name];
			if (msg.name === "record_enabled") {
				state.exportReason = msg.reason || "";
				noteExport(!!state.local.record_enabled);
			}
			if (msg.name === "source" || msg.name === "folder") {
				state.midError = msg.reason || "";
				state.midKind = msg.name;
			}
			return state;
		}

		function linkUp() {
			state.link = "up";
			return state;
		}

		function linkLost() {
			state.link = "down";
			stopHold("gate", false);
			stopHold("erase", false);
			stopHold("cue", false);
			state.fingers.gate = false;
			state.fingers.erase = false;
			state.fingers.cue = false;
			release(Array.from(state.grabbed));
			return state;
		}

		function locked(name) {
			return state.grabbed.has(name);
		}

		function markExport(on) {
			noteExport(on);
			return state;
		}

		return {
			state: state,
			fingerDown: fingerDown,
			value: value,
			fingerUp: fingerUp,
			holdDown: holdDown,
			holdUp: holdUp,
			panic: panic,
			frame: frame,
			reject: reject,
			linkUp: linkUp,
			linkLost: linkLost,
			locked: locked,
			markExport: markExport,
		};
	}

	return { create: create };
});
