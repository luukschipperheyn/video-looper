#pragma once

class Instrument;

struct KeysDown {
	bool showUi = false;
	bool digit[10] = {};
	bool sourcePrev = false;
	bool sourceNext = false;
	bool blendPrev = false;
	bool blendNext = false;
	bool fxPrev = false;
	bool fxNext = false;
	bool randomVideo = false;
};

struct HotkeyHolds {
	bool overdub = false;
	bool cue = false;
};

class Hotkeys {
public:
	// One frame. textField: a text field has focus. No edge fires, and both holds stay closed.
	// Edge memory updates even then, so a key held through focus does not fire when focus leaves.
	HotkeyHolds frame(Instrument& instrument, const KeysDown& down, HotkeyHolds holds, bool textField);

private:
	KeysDown was;
};
