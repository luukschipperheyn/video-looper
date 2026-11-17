#pragma once

#include <cstdint>

// Optional Ableton Link integration.
// When USE_ABLETON_LINK is defined and the Ableton::Link library is available,
// this bridge will forward to the real implementation. Otherwise it falls back
// to a local stub so the app still builds and runs without Link.

class LinkBridge {
public:
	LinkBridge();
	~LinkBridge();

	void tick(double dt);

	void setEnabled(bool enabled);
	bool isEnabled() const;

	void setTempo(double bpm);
	double getTempo() const;

	void setQuantum(double quantum);

	// Query current musical time. When Link is enabled, this queries the
	// shared session state. Otherwise it uses a local stub.
	void getBeatTime(double quantum, double& outBeats, double& outPhase, double& outBpm) const;

	// Returns the number of connected peers (0 when disabled or unavailable)
	int getNumPeers() const;

	// Optional start/stop sync controls (no-op in stub)
	void setStartStopSyncEnabled(bool enabled);
	bool isStartStopSyncEnabled() const;
	void setPlaying(bool isPlaying);
	bool isPlaying() const;

	// Returns true if the Ableton Link SDK is compiled in
	bool isAvailable() const;

private:
	// Stub state when USE_ABLETON_LINK is not enabled
	bool enabled;
	double tempoBpm;
	double quantumBeats;
	bool startStopSync;
	bool playing;
	double stubBeats;

#ifdef USE_ABLETON_LINK
	// Forward declaration to avoid including heavy headers in this file
	struct Impl;
	Impl* impl;
#endif
};
