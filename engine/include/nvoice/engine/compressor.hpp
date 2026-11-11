// N Voice for Studio - Audio Engine (OBS-independent)
// Feed-forward compressor with a soft knee and linked channels.
//
// Parameters may be changed from any thread at any time; the audio thread
// picks them up at the start of the next block.
#pragma once

#include "processor.hpp"

#include <atomic>

namespace nvoice::engine {

class Compressor final : public AudioProcessor {
public:
	struct Settings {
		float thresholdDb;
		float ratio;
		float attackMs;
		float releaseMs;
		float makeupDb;
	};

	static constexpr float kKneeDb = 6.0f;

	Compressor();

	// Any thread. Values are clamped to the parameter table ranges.
	void setSettings(const Settings &settings) noexcept;
	Settings settings() const noexcept;
	void setEnabled(bool enabled) noexcept;
	bool enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

	// UI thread. Deepest gain reduction (dB, <= 0) since the previous call.
	float readGainReductionDb() noexcept;

	// Static gain computer: gain change in dB (<= 0) for an input level.
	static float computeGainDb(float inputDb, float thresholdDb, float ratio) noexcept;

	// Makeup that leaves `referenceDb` (normal speech) where it was, so the
	// compressor only reshapes what is louder or quieter than normal.
	static float autoMakeupDb(float thresholdDb, float ratio, float referenceDb) noexcept
	{
		return -computeGainDb(referenceDb, thresholdDb, ratio);
	}

	void prepare(double sampleRate, std::uint32_t numChannels) override;
	void reset() noexcept override;
	void process(AudioBlock &block) noexcept override;

private:
	void loadSettings() noexcept; // audio thread

	double sampleRate_ = 48000.0;

	std::atomic<float> threshold_;
	std::atomic<float> ratio_;
	std::atomic<float> attack_;
	std::atomic<float> release_;
	std::atomic<float> makeup_;
	std::atomic<bool> enabled_{false};
	std::atomic<float> reductionSinceRead_{0.0f};

	// Audio thread state
	bool wasEnabled_ = false;
	float gainDb_ = 0.0f; // smoothed gain reduction (<= 0)
	float curThreshold_ = 0.0f;
	float curRatio_ = 1.0f;
	float curMakeupDb_ = 0.0f;
	float attackCoef_ = 0.0f;
	float releaseCoef_ = 0.0f;
};

} // namespace nvoice::engine
