#include "nvoice/engine/peak_limiter.hpp"

#include "nvoice/engine/db.hpp"
#include "nvoice/engine/levels.hpp"

#include <algorithm>
#include <cmath>

namespace nvoice::engine {

namespace {
void atomicMin(std::atomic<float> &target, float value) noexcept
{
	float current = target.load(std::memory_order_relaxed);
	while (value < current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
	}
}
} // namespace

PeakLimiter::PeakLimiter() : ceilingDb_(levels::kCeilingDb) {}

void PeakLimiter::setCeilingDb(float db) noexcept
{
	ceilingDb_.store(std::clamp(db, -24.0f, 0.0f), std::memory_order_relaxed);
}

float PeakLimiter::readGainReductionDb() noexcept
{
	return reductionSinceRead_.exchange(0.0f, std::memory_order_relaxed);
}

void PeakLimiter::prepare(double sampleRate, std::uint32_t numChannels)
{
	const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
	lookahead_ = static_cast<std::uint32_t>(std::max(1.0, std::round(sr * kLookaheadSec)));
	channels_ = std::clamp<std::uint32_t>(numChannels, 1, kMaxChannels);
	releaseCoef_ = static_cast<float>(std::exp(-1.0 / (kReleaseSec * sr)));
	delay_.assign(static_cast<std::size_t>(lookahead_) * channels_, 0.0f);
	deque_.assign(lookahead_ + 2, Entry{0, 1.0f});
	reset();
}

void PeakLimiter::reset() noexcept
{
	std::fill(delay_.begin(), delay_.end(), 0.0f);
	pos_ = 0;
	head_ = 0;
	size_ = 0;
	sampleIndex_ = 0;
	gain_ = 1.0f;
	reductionSinceRead_.store(0.0f, std::memory_order_relaxed);
}

void PeakLimiter::process(AudioBlock &block) noexcept
{
	if (block.channels == nullptr || block.numFrames == 0 || delay_.empty())
		return;

	const std::uint32_t channels = std::min(block.numChannels, channels_);
	const float ceiling = dbToLinear(ceilingDb_.load(std::memory_order_relaxed));
	const auto capacity = static_cast<std::uint32_t>(deque_.size());
	float deepest = 1.0f;

	for (std::uint32_t i = 0; i < block.numFrames; ++i) {
		// 1. Gain the incoming sample would need (channels linked).
		float peak = 0.0f;
		for (std::uint32_t c = 0; c < channels; ++c) {
			if (const float *ch = block.channels[c])
				peak = std::max(peak, std::fabs(ch[i]));
		}
		const float required = peak > ceiling ? ceiling / peak : 1.0f;

		// 2. Sliding minimum over the look-ahead window [now - L, now].
		while (size_ > 0) {
			const auto back = (head_ + size_ - 1) % capacity;
			if (deque_[back].gain < required)
				break;
			--size_;
		}
		deque_[(head_ + size_) % capacity] = Entry{sampleIndex_, required};
		++size_;
		while (deque_[head_].index + lookahead_ < sampleIndex_) {
			head_ = (head_ + 1) % capacity;
			--size_;
		}
		const float windowMin = deque_[head_].gain;

		// 3. Drop at once to what the window needs; recover smoothly.
		const float released = releaseCoef_ * gain_ + (1.0f - releaseCoef_);
		gain_ = std::min(windowMin, released);
		deepest = std::min(deepest, gain_);

		// 4. Output the delayed sample, store the new one.
		float *slot = &delay_[static_cast<std::size_t>(pos_) * channels_];
		for (std::uint32_t c = 0; c < channels; ++c) {
			if (float *ch = block.channels[c]) {
				const float in = ch[i];
				ch[i] = slot[c] * gain_;
				slot[c] = in;
			}
		}
		pos_ = pos_ + 1 == lookahead_ ? 0 : pos_ + 1;
		++sampleIndex_;
	}

	if (deepest < 1.0f)
		atomicMin(reductionSinceRead_, linearToDb(deepest));
}

} // namespace nvoice::engine
