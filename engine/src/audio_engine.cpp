#include "nvoice/engine/audio_engine.hpp"

namespace nvoice::engine {

void AudioEngine::prepare(double sampleRate, std::uint32_t numChannels)
{
	sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
	numChannels_ = numChannels == 0 ? 1 : (numChannels > kMaxChannels ? kMaxChannels : numChannels);

	inputMeter_.prepare(sampleRate_);
	outputMeter_.prepare(sampleRate_);

	for (std::size_t i = 0; i < stageCount_; ++i)
		stages_[i]->prepare(sampleRate_, numChannels_);
}

void AudioEngine::reset() noexcept
{
	inputMeter_.reset();
	outputMeter_.reset();
	for (std::size_t i = 0; i < stageCount_; ++i)
		stages_[i]->reset();
}

bool AudioEngine::addStage(AudioProcessor *stage) noexcept
{
	if (stage == nullptr || stageCount_ >= kMaxStages)
		return false;
	stages_[stageCount_++] = stage;
	return true;
}

void AudioEngine::process(AudioBlock &block) noexcept
{
	if (block.channels == nullptr || block.numFrames == 0)
		return;

	inputMeter_.process(block);

	for (std::size_t i = 0; i < stageCount_; ++i)
		stages_[i]->process(block);

	outputMeter_.process(block);
}

} // namespace nvoice::engine
