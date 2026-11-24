#include "ui/ParamBus.hpp"

#include "control/ControlTable.hpp"

#include <cmath>

ParamBus::ParamBus() {
	for (const Control& c : kControls) {
		switch (c.type) {
		case ControlType::Bool:
			bools[c.name] = c.def != 0.0f;
			break;
		case ControlType::Float:
			floats[c.name] = c.def;
			break;
		case ControlType::Int:
			ints[c.name] = int(std::lround(c.def));
			break;
		case ControlType::String:
			strings[c.name] = {};
			break;
		}
	}
}
