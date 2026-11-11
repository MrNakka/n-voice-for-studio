// N Voice for Studio - Audio Engine (OBS-independent)
// Look-ahead peak limiter: the last stage, so no sample leaves the filter
// above the ceiling (levels::kCeilingDb), whatever happens before it.
//
// The audio is delayed by the look-ahead time (1.5 ms) so the gain can come
// down before a peak arrives. Below the ceiling the signal passes unchanged
// (just delayed).
#pragma once

#include "processor.hpp"

#include <atomic>
#include <vector>

namespace nvoice::engine {

class PeakLimiter final : public AudioProcessor {
public:
	static constexpr double kLookaheadSec = 0.0015;
	static constexpr double kReleaseSec = 0.08;

	PeakLimiter();

	void setCeilingDb(float db) noexcept;
	float ceilingDb() const noexcept { return ceilingDb_.load(std::memory_order_relaxed); }

	// UI thread. Deepest gain reduction (dB, <= 0) since the previous call.
	float readGainReductionDb() noexcept;

	std::uint32_t latencyFrames() const noexcept { return lookahead_; }

	void prepare(double sampleRate, std::uint32_t numChannels) override;
	void reset() noexcept override;
	void process(AudioBlock &block) noexcept override;

private:
	std::atomic<float> ceilingDb_;
	std::atomic<float> reductionSinceRead_{0.0f};

	std::uint32_t lookahead_ = 72;
	std::uint32_t channels_ = 2;
	float releaseCoef_ = 0.0f;

	// Delay line: lookahead_ frames per channel (interleaved by channel).
	std::vector<float> delay_;
	std::uint32_t pos_ = 0;

	// Sliding-window minimum of the required gain over the look-ahead,
	// kept as a monotonic deque in a fixed ring (no allocation in process).
	struct Entry {
		std::uint64_t index;
		float gain;
	};
	std::vector<Entry> deque_;
	std::uint32_t head_ = 0;
	std::uint32_t size_ = 0;
	std::uint64_t sampleIndex_ = 0;

	float gain_ = 1.0f;
};

} // namespace nvoice::engine
