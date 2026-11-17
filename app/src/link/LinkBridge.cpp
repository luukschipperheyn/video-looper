#include "link/LinkBridge.hpp"
#include <cmath>

#ifdef USE_ABLETON_LINK
#include <ableton/Link.hpp>
#include <chrono>

struct LinkBridge::Impl {
	ableton::Link link;

	Impl(double initialTempo) : link(initialTempo) {}
};
#endif

LinkBridge::LinkBridge()
	: enabled(false), tempoBpm(120.0), quantumBeats(4.0), startStopSync(false), playing(false), stubBeats(0.0)
#ifdef USE_ABLETON_LINK
	  ,
	  impl(new Impl(tempoBpm))
#endif
{
#ifdef USE_ABLETON_LINK
	impl->link.enable(enabled);
#endif
}

void LinkBridge::tick(double dt) {
#ifndef USE_ABLETON_LINK
	// Advance local stub
	stubBeats += (tempoBpm / 60.0) * dt;
#else
	(void)dt;
#endif
}

LinkBridge::~LinkBridge() {
#ifdef USE_ABLETON_LINK
	delete impl;
	impl = nullptr;
#endif
}

void LinkBridge::setEnabled(bool e) {
	enabled = e;
#ifdef USE_ABLETON_LINK
	impl->link.enable(enabled);
#endif
}

bool LinkBridge::isEnabled() const {
	return enabled;
}

void LinkBridge::setTempo(double bpm) {
	tempoBpm = bpm;
#ifdef USE_ABLETON_LINK
	using Clock = std::chrono::microseconds;
	auto state = impl->link.captureAppSessionState();
	const auto now = impl->link.clock().micros();
	state.setTempo(bpm, now);
	impl->link.commitAppSessionState(state);
#endif
}

double LinkBridge::getTempo() const {
#ifdef USE_ABLETON_LINK
	auto state = impl->link.captureAppSessionState();
	return state.tempo();
#else
	return tempoBpm;
#endif
}

void LinkBridge::setQuantum(double quantum) {
	quantumBeats = quantum;
}

int LinkBridge::getNumPeers() const {
#ifdef USE_ABLETON_LINK
	return static_cast<int>(impl->link.numPeers());
#else
	return 0;
#endif
}

void LinkBridge::setStartStopSyncEnabled(bool e) {
	startStopSync = e;
}

bool LinkBridge::isStartStopSyncEnabled() const {
	return startStopSync;
}

void LinkBridge::setPlaying(bool isPlaying) {
	playing = isPlaying;
#ifdef USE_ABLETON_LINK
	auto state = impl->link.captureAppSessionState();
	const auto now = impl->link.clock().micros();
	state.setIsPlaying(isPlaying, now);
	impl->link.commitAppSessionState(state);
#endif
}

bool LinkBridge::isPlaying() const {
	return playing;
}

void LinkBridge::getBeatTime(double quantum, double& outBeats, double& outPhase, double& outBpm) const {
#ifdef USE_ABLETON_LINK
	auto state = impl->link.captureAppSessionState();
	using Clock = std::chrono::microseconds;
	const auto now = impl->link.clock().micros();
	outBpm = state.tempo();
	// beatAtTime returns beats; phaseAtTime returns phase within quantum
	outBeats = state.beatAtTime(now, quantum);
	outPhase = state.phaseAtTime(now, quantum);
#else
	outBpm = tempoBpm;
	outBeats = stubBeats;
	double ph = fmod(stubBeats, quantum);
	outPhase = ph;
#endif
}

bool LinkBridge::isAvailable() const {
#ifdef USE_ABLETON_LINK
	return true;
#else
	return false;
#endif
}
