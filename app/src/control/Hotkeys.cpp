#include "control/Hotkeys.hpp"

#include "control/Instrument.hpp"

HotkeyHolds Hotkeys::frame(Instrument& instrument, const KeysDown& down, HotkeyHolds holds, bool textField) {
	if (textField)
		holds = {};
	else {
		if (down.showUi && !was.showUi) instrument.toggle("show_ui");
		for (int d = 0; d < 10; ++d)
			if (down.digit[d] && !was.digit[d])
				instrument.setNumber("record_blend_opacity", d == 0 ? 1.0 : d / 10.0);
		if (down.sourcePrev && !was.sourcePrev) instrument.stepSourceKind(-1);
		if (down.sourceNext && !was.sourceNext) instrument.stepSourceKind(+1);
		if (down.blendPrev && !was.blendPrev) instrument.nudge("record_blend_mode", -1);
		if (down.blendNext && !was.blendNext) instrument.nudge("record_blend_mode", 1);
		if (down.fxPrev && !was.fxPrev) instrument.nudge("fx_mode", -1);
		if (down.fxNext && !was.fxNext) instrument.nudge("fx_mode", 1);
		if (down.randomVideo && !was.randomVideo) instrument.randomVideo();
	}
	was = down;
	return holds;
}
