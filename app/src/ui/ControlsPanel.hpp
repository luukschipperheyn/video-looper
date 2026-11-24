#pragma once

#include "buffer/RingBuffer.hpp"
#include "control/Instrument.hpp"
#include "io/FolderList.hpp"
#include "io/SourceController.hpp"
#include "link/LinkBridge.hpp"
#include "remote/WebInterface.hpp"
#include "ui/ParamBus.hpp"

// The Mac controls. Reads values from params and asks the instrument for every change, so a control behaves
// here as it does on the Web interface.
class ControlsPanel {
public:
	void draw(const ParamBus& params, Instrument& instrument, const LinkBridge& link, const RingBuffer& ring, float dt,
	          const SourceController& source, const WebInterface& web, const FolderList& folders);

private:
	std::string filePathBuf;
	bool filePathActive = false;
};
