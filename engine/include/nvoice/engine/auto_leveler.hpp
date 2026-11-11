// N Voice for Studio - Audio Engine (OBS-independent)
// Slow automatic gain that keeps speech near a target level.
//
// The gain only moves while the input is above the gate ("無音判定"), so
// pauses and room noise are not pushed up. Reductions move faster than
// boosts so a sudden loud voice is tamed quickly without pumping on pauses.
#pragma once

#include "processor.hpp"

#include <atomic>

namespace nvoice::engine {

class AutoLeveler final : public AudioProcessor {
public:
	struct Settings {
		float targetDb;  // desired speech level (RMS, dBFS)
		float maxGainDb; // most boost for quiet speech
		float maxCutDb;  // most reduction for loud speech (positive number)
		float speedSec;  // time to follow a level change
		float gateDb;    // below this the input counts as silence
	};

	// Integration time of the speech level that the gain follows.
	static constexpr double kDetectorSec = 0.4;
	// Integration time of the gate's level. Short so the gain freezes as soon
	// as speech stops, instead of creeping up while the slow detector decays.
	static constexpr double kGateDetectorSec = 0.05;
	// Speech counts as ending when the fast level falls this far below the
	// slow level; the gain is frozen through that decay too.
	static constexpr float kDecayHoldDb = 6.0f;
	// Reductions follow this much faster than boosts.
	static constexpr double kCutSpeedup = 4.0;

	AutoLeveler();

	void setSettings(const Settings &settings) noexcept;
	Settings settings() const noexcept;
	void setEnabled(bool enabled) noexcept;
	bool enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

	// UI thread. Gain currently applied (dB, positive = boost).
	float currentGainDb() const noexcept { return publishedGain_.load(std::memory_order_relaxed); }

	void prepare(double sampleRate, std::uint32_t numChannels) override;
	void reset() noexcept override;
	void process(AudioBlock &block) noexcept override;

private:
	double sampleRate_ = 48000.0;

	std::atomic<float> target_;
	std::atomic<float> maxGain_;
	std::atomic<float> maxCut_;
	std::atomic<float> speed_;
	std::atomic<float> gate_;
	std::atomic<bool> enabled_{false};
	std::atomic<float> publishedGain_{0.0f};

	// Audio thread state
	bool wasEnabled_ = false;
	double meanSquare_ = 0.0;
	double gateMeanSquare_ = 0.0;
	float gainDb_ = 0.0f;
};

} // namespace nvoice::engine
