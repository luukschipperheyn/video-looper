#pragma once

struct BeatTime { double beats; double phase; double bpm; };

class Clock {
public:
	Clock() : bpm(120.0), quantum(4.0), beats(0.0) {}
	void setQuantum(double q) { quantum = q; }
	void requestTempo(double b) { bpm = b; }
	BeatTime now(double dt) {
		beats += (bpm / 60.0) * dt; // beats advanced by dt
		double ph = fmod(beats, quantum);
		return BeatTime{beats, ph, bpm};
	}

private:
	double bpm;
	double quantum;
	double beats;
};


