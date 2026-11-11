#include "nvoice/engine/compressor.hpp"

#include "nvoice/engine/db.hpp"
#include "nvoice/engine/parameters.hpp"

#include <algorithm>
#include <cmath>

namespace nvoice::engine {

namespace {

float timeCoefficient(float timeMs, double sampleRate) noexcept
{
	const double samples = static_cast<double>(timeMs) * 0.001 * sampleRate;
	return samples <= 1.0 ? 0.0f : static_cast<float>(std::exp(-1.0 / samples));
}

void atomicMin(std::atomic<float> &target, float value) noexcept
{
	float current = target.load(std::memory_order_relaxed);
	while (value < current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
	}
}

} // namespace

Compressor::Compressor()
	: threshold_(params::kCompThreshold.defaultValue),
	  ratio_(params::kCompRatio.defaultValue),
	  attack_(params::kCompAttack.defaultValue),
	  release_(params::kCompRelease.defaultValue),
	  makeup_(params::kCompMakeup.defaultValue)
{
}

void Compressor::setSettings(const Settings &s) noexcept
{
	threshold_.store(params::kCompThreshold.clamp(s.thresholdDb), std::memory_order_relaxed);
	ratio_.store(params::kCompRatio.clamp(s.ratio), std::memory_order_relaxed);
	attack_.store(params::kCompAttack.clamp(s.attackMs), std::memory_order_relaxed);
	release_.store(params::kCompRelease.clamp(s.releaseMs), std::memory_order_relaxed);
	makeup_.store(params::kCompMakeup.clamp(s.makeupDb), std::memory_order_relaxed);
}

Compressor::Settings Compressor::settings() const noexcept
{
	return {threshold_.load(std::memory_order_relaxed), ratio_.load(std::memory_order_relaxed),
		attack_.load(std::memory_order_relaxed), release_.load(std::memory_order_relaxed),
		makeup_.load(std::memory_order_relaxed)};
}

void Compressor::setEnabled(bool enabled) noexcept
{
	enabled_.store(enabled, std::memory_order_relaxed);
}

float Compressor::readGainReductionDb() noexcept
{
	return reductionSinceRead_.exchange(0.0f, std::memory_order_relaxed);
}

float Compressor::computeGainDb(float x, float threshold, float ratio) noexcept
{
	// Soft-knee gain computer (Giannoulis, Massberg & Reiss 2012).
	const float slope = 1.0f / ratio - 1.0f; // <= 0
	const float over = x - threshold;
	if (2.0f * over < -kKneeDb)
		return 0.0f;
	if (2.0f * std::fabs(over) <= kKneeDb) {
		const float k = over + kKneeDb * 0.5f;
		return slope * k * k / (2.0f * kKneeDb);
	}
	return slope * over;
}

void Compressor::prepare(double sampleRate, std::uint32_t)
{
	sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
	reset();
}

void Compressor::reset() noexcept
{
	gainDb_ = 0.0f;
	reductionSinceRead_.store(0.0f, std::memory_order_relaxed);
}

void Compressor::loadSettings() noexcept
{
	curThreshold_ = threshold_.load(std::memory_order_relaxed);
	curRatio_ = ratio_.load(std::memory_order_relaxed);
	curMakeupDb_ = makeup_.load(std::memory_order_relaxed);
	attackCoef_ = timeCoefficient(attack_.load(std::memory_order_relaxed), sampleRate_);
	releaseCoef_ = timeCoefficient(release_.load(std::memory_order_relaxed), sampleRate_);
}

void Compressor::process(AudioBlock &block) noexcept
{
	const bool enabled = enabled_.load(std::memory_order_relaxed);
	if (enabled && !wasEnabled_)
		reset(); // start from a clean envelope when switched on
	wasEnabled_ = enabled;
	if (!enabled || block.channels == nullptr || block.numFrames == 0)
		return;

	loadSettings();

	const std::uint32_t channels = std::min(block.numChannels, kMaxChannels);
	float deepest = 0.0f;

	for (std::uint32_t i = 0; i < block.numFrames; ++i) {
		// Linked detection: the loudest channel drives every channel, so the
		// stereo image does not shift.
		float peak = 0.0f;
		for (std::uint32_t c = 0; c < channels; ++c) {
			if (const float *ch = block.channels[c])
				peak = std::max(peak, std::fabs(ch[i]));
		}

		const float target = computeGainDb(linearToDb(peak), curThreshold_, curRatio_);
		const float coef = target < gainDb_ ? attackCoef_ : releaseCoef_;
		gainDb_ = coef * gainDb_ + (1.0f - coef) * target;
		deepest = std::min(deepest, gainDb_);

		const float gain = dbToLinear(gainDb_ + curMakeupDb_);
		for (std::uint32_t c = 0; c < channels; ++c) {
			if (float *ch = block.channels[c])
				ch[i] *= gain;
		}
	}

	atomicMin(reductionSinceRead_, deepest);
}

} // namespace nvoice::engine
