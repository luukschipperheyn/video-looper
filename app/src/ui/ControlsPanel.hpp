#pragma once

#include "ui/ParamBus.hpp"
#include "link/LinkBridge.hpp"
#include "buffer/RingBuffer.hpp"
#include "io/FolderList.hpp"
#include "io/SourceController.hpp"
#include "remote/RemoteSession.hpp"

class ControlsPanel {
public:
	void draw(ParamBus& params, LinkBridge& link, RingBuffer& ring, float dt, float& outBpm, float& outQuantum,
	          SourceController* source, const RemoteSession& remote, FolderList& folders);
};
