// N Voice for Studio - Audio Engine (OBS-independent)
// Real-time safe level meter.
//
// Written from the audio thread (process), read from the UI thread (read).
// No locks and no allocation: values are published through atomics.
#pragma once

#include "audio_block.hpp"

#include <atomic>

namespace nvoice::engine {

struct LevelReading {
	float rmsDb;  // smoothed RMS across all channels (dBFS)
	float peakDb; // highest sample peak since the previous read (dBFS)
};

class LevelMeter {
public:
	// rmsTimeConstantSec: integration time of the smoothed RMS.
	void prepare(double sampleRate, double rmsTimeConstantSec = 0.3) noexcept;
	void reset() noexcept;

	// Audio thread. Measures the block without modifying it.
	void process(const AudioBlock &block) noexcept;

	// UI thread. Returns the current RMS and the peak accumulated since the
	// last call, then clears the peak accumulator. Intended for one reader.
	LevelReading read() noexcept;

	// UI thread. Same as read() but leaves the peak accumulator untouched.
	LevelReading peek() const noexcept;

private:
	double sampleRate_ = 48000.0;
	double rmsTimeConstant_ = 0.3;
	double meanSquare_ = 0.0; // audio thread only

	std::atomic<float> publishedRms_{0.0f};
	std::atomic<float> peakSinceRead_{0.0f};
};

} // namespace nvoice::engine
