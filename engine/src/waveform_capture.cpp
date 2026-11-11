#include "nvoice/engine/waveform_capture.hpp"

#include <algorithm>
#include <cmath>

namespace nvoice::engine {

void WaveformCapture::prepare(double sampleRate, double bucketSec) noexcept
{
	const double frames = std::round((sampleRate > 0.0 ? sampleRate : 48000.0) * bucketSec);
	bucketFrames_ = static_cast<std::uint32_t>(std::max(1.0, frames));
	restart();
}

void WaveformCapture::restart() noexcept
{
	count_ = 0;
	min_ = 0.0f;
	max_ = 0.0f;
}

void WaveformCapture::process(const AudioBlock &block) noexcept
{
	if (block.channels == nullptr || block.numFrames == 0)
		return;
	const std::uint32_t channels = std::min(block.numChannels, kMaxChannels);

	for (std::uint32_t i = 0; i < block.numFrames; ++i) {
		for (std::uint32_t c = 0; c < channels; ++c) {
			if (const float *ch = block.channels[c]) {
				min_ = std::min(min_, ch[i]);
				max_ = std::max(max_, ch[i]);
			}
		}
		if (++count_ == bucketFrames_) {
			const WaveBucket bucket{min_, max_};
			ring_.push(&bucket, 1);
			restart();
		}
	}
}

} // namespace nvoice::engine
