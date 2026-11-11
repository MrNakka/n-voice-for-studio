#include "nvoice/engine/auto_leveler.hpp"

#include "nvoice/engine/db.hpp"
#include "nvoice/engine/parameters.hpp"

#include <algorithm>
#include <cmath>

namespace nvoice::engine {

AutoLeveler::AutoLeveler()
	: target_(params::kLevelerTarget.defaultValue),
	  maxGain_(params::kLevelerMaxGain.defaultValue),
	  maxCut_(params::kLevelerMaxCut.defaultValue),
	  speed_(params::kLevelerSpeed.defaultValue),
	  gate_(params::kLevelerGate.defaultValue)
{
}

void AutoLeveler::setSettings(const Settings &s) noexcept
{
	target_.store(params::kLevelerTarget.clamp(s.targetDb), std::memory_order_relaxed);
	maxGain_.store(params::kLevelerMaxGain.clamp(s.maxGainDb), std::memory_order_relaxed);
	maxCut_.store(params::kLevelerMaxCut.clamp(s.maxCutDb), std::memory_order_relaxed);
	speed_.store(params::kLevelerSpeed.clamp(s.speedSec), std::memory_order_relaxed);
	gate_.store(params::kLevelerGate.clamp(s.gateDb), std::memory_order_relaxed);
}

AutoLeveler::Settings AutoLeveler::settings() const noexcept
{
	return {target_.load(std::memory_order_relaxed), maxGain_.load(std::memory_order_relaxed),
		maxCut_.load(std::memory_order_relaxed), speed_.load(std::memory_order_relaxed),
		gate_.load(std::memory_order_relaxed)};
}

void AutoLeveler::setEnabled(bool enabled) noexcept
{
	enabled_.store(enabled, std::memory_order_relaxed);
}

void AutoLeveler::prepare(double sampleRate, std::uint32_t)
{
	sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
	reset();
}

void AutoLeveler::reset() noexcept
{
	meanSquare_ = 0.0;
	gateMeanSquare_ = 0.0;
	gainDb_ = 0.0f;
	publishedGain_.store(0.0f, std::memory_order_relaxed);
}

void AutoLeveler::process(AudioBlock &block) noexcept
{
	const bool enabled = enabled_.load(std::memory_order_relaxed);
	if (enabled && !wasEnabled_)
		reset();
	wasEnabled_ = enabled;
	if (!enabled || block.channels == nullptr || block.numFrames == 0)
		return;

	const std::uint32_t channels = std::min(block.numChannels, kMaxChannels);
	const std::uint32_t frames = block.numFrames;

	// 1. Measure the input (before gain) for this block.
	double sumSquares = 0.0;
	std::uint32_t measured = 0;
	for (std::uint32_t c = 0; c < channels; ++c) {
		const float *ch = block.channels[c];
		if (ch == nullptr)
			continue;
		++measured;
		for (std::uint32_t i = 0; i < frames; ++i)
			sumSquares += static_cast<double>(ch[i]) * ch[i];
	}
	if (measured == 0)
		return;

	const double blockMs = sumSquares / (static_cast<double>(frames) * measured);
	const double detAlpha = std::exp(-static_cast<double>(frames) / (kDetectorSec * sampleRate_));
	meanSquare_ = detAlpha * meanSquare_ + (1.0 - detAlpha) * blockMs;
	const double gateAlpha = std::exp(-static_cast<double>(frames) / (kGateDetectorSec * sampleRate_));
	gateMeanSquare_ = gateAlpha * gateMeanSquare_ + (1.0 - gateAlpha) * blockMs;

	auto toDb = [](double ms) {
		return ms > 0.0 ? static_cast<float>(10.0 * std::log10(ms)) : kSilenceDb;
	};
	const float levelDb = toDb(meanSquare_);
	const float gateLevelDb = toDb(gateMeanSquare_);

	// 2. Move the gain toward the target, but only while someone is speaking.
	const float startGain = gainDb_;
	const bool speaking = gateLevelDb > gate_.load(std::memory_order_relaxed) &&
			      gateLevelDb > levelDb - kDecayHoldDb;
	if (speaking) {
		const float desired = std::clamp(target_.load(std::memory_order_relaxed) - levelDb,
						 -maxCut_.load(std::memory_order_relaxed),
						 maxGain_.load(std::memory_order_relaxed));
		double tau = speed_.load(std::memory_order_relaxed);
		if (desired < gainDb_)
			tau /= kCutSpeedup;
		const double alpha = std::exp(-static_cast<double>(frames) / (tau * sampleRate_));
		gainDb_ = static_cast<float>(alpha * gainDb_ + (1.0 - alpha) * desired);
	}

	// Limits may have been lowered while the gain was outside them.
	gainDb_ =
		std::clamp(gainDb_, -maxCut_.load(std::memory_order_relaxed), maxGain_.load(std::memory_order_relaxed));

	// 3. Apply, ramping across the block to avoid zipper noise.
	const float g0 = dbToLinear(startGain);
	const float g1 = dbToLinear(gainDb_);
	const float step = (g1 - g0) / static_cast<float>(frames);
	for (std::uint32_t c = 0; c < channels; ++c) {
		float *ch = block.channels[c];
		if (ch == nullptr)
			continue;
		float g = g0;
		for (std::uint32_t i = 0; i < frames; ++i) {
			g += step;
			ch[i] *= g;
		}
	}

	publishedGain_.store(gainDb_, std::memory_order_relaxed);
}

} // namespace nvoice::engine
