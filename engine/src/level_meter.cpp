#include "nvoice/engine/level_meter.hpp"

#include "nvoice/engine/db.hpp"

#include <cmath>

namespace nvoice::engine {

void LevelMeter::prepare(double sampleRate, double rmsTimeConstantSec) noexcept
{
	sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
	rmsTimeConstant_ = rmsTimeConstantSec > 0.0 ? rmsTimeConstantSec : 0.3;
	reset();
}

void LevelMeter::reset() noexcept
{
	meanSquare_ = 0.0;
	publishedRms_.store(0.0f, std::memory_order_relaxed);
	peakSinceRead_.store(0.0f, std::memory_order_relaxed);
}

void LevelMeter::process(const AudioBlock &block) noexcept
{
	if (block.numFrames == 0 || block.channels == nullptr)
		return;

	float blockPeak = 0.0f;
	double sumSquares = 0.0;
	std::uint32_t measuredChannels = 0;

	const std::uint32_t channels = block.numChannels < kMaxChannels ? block.numChannels : kMaxChannels;
	for (std::uint32_t c = 0; c < channels; ++c) {
		const float *samples = block.channels[c];
		if (samples == nullptr)
			continue;
		++measuredChannels;
		for (std::uint32_t i = 0; i < block.numFrames; ++i) {
			const float s = samples[i];
			const float a = std::fabs(s);
			if (a > blockPeak)
				blockPeak = a;
			sumSquares += static_cast<double>(s) * s;
		}
	}

	if (measuredChannels == 0)
		return;

	// One-pole smoothing of the mean square, applied per block so the time
	// constant holds regardless of block size.
	const double blockMeanSquare = sumSquares / (static_cast<double>(block.numFrames) * measuredChannels);
	const double alpha = std::exp(-static_cast<double>(block.numFrames) / (rmsTimeConstant_ * sampleRate_));
	meanSquare_ = alpha * meanSquare_ + (1.0 - alpha) * blockMeanSquare;
	publishedRms_.store(static_cast<float>(std::sqrt(meanSquare_)), std::memory_order_relaxed);

	// Lock-free running maximum. The UI thread resets it to 0 on read.
	float current = peakSinceRead_.load(std::memory_order_relaxed);
	while (blockPeak > current &&
	       !peakSinceRead_.compare_exchange_weak(current, blockPeak, std::memory_order_relaxed)) {
	}
}

LevelReading LevelMeter::read() noexcept
{
	const float rms = publishedRms_.load(std::memory_order_relaxed);
	const float peak = peakSinceRead_.exchange(0.0f, std::memory_order_relaxed);
	return {linearToDb(rms), linearToDb(peak)};
}

LevelReading LevelMeter::peek() const noexcept
{
	return {linearToDb(publishedRms_.load(std::memory_order_relaxed)),
		linearToDb(peakSinceRead_.load(std::memory_order_relaxed))};
}

} // namespace nvoice::engine
