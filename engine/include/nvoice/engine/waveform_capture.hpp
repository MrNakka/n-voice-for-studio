// N Voice for Studio - Audio Engine (OBS-independent)
// Reduces audio to min/max buckets (default 10 ms) for waveform display and
// hands them to the UI through a lock-free ring buffer.
//
// The audio thread only compares samples and occasionally pushes 8 bytes,
// so drawing a waveform costs the audio path almost nothing.
#pragma once

#include "audio_block.hpp"
#include "spsc_ring_buffer.hpp"

#include <cstddef>
#include <cstdint>

namespace nvoice::engine {

struct WaveBucket {
	float min;
	float max;
};

class WaveformCapture {
public:
	// ~10 s of 10 ms buckets before the UI has to drain them.
	explicit WaveformCapture(std::size_t capacity = 1024) : ring_(capacity) {}

	void prepare(double sampleRate, double bucketSec = 0.01) noexcept;

	// Audio thread. Drops any partial bucket (call when capture restarts).
	void restart() noexcept;

	// Audio thread. Folds the block into buckets; pushes each completed one.
	// If the UI stops draining, new buckets are dropped (never blocks).
	void process(const AudioBlock &block) noexcept;

	// UI thread. Pops up to `maxCount` completed buckets, oldest first.
	std::size_t read(WaveBucket *out, std::size_t maxCount) noexcept { return ring_.pop(out, maxCount); }

	std::uint32_t bucketFrames() const noexcept { return bucketFrames_; }

private:
	SpscRingBuffer<WaveBucket> ring_;
	std::uint32_t bucketFrames_ = 480;
	std::uint32_t count_ = 0;
	float min_ = 0.0f;
	float max_ = 0.0f;
};

} // namespace nvoice::engine
