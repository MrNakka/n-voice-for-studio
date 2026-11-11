// N Voice for Studio - Audio Engine (OBS-independent)
// One AudioEngine instance per OBS filter instance. Owns the DSP chain and
// the input/output meters.
//
// Phase 1: the chain is empty, so the engine is a metered pass-through.
#pragma once

#include "audio_block.hpp"
#include "level_meter.hpp"
#include "processor.hpp"

#include <array>
#include <cstddef>

namespace nvoice::engine {

class AudioEngine {
public:
	static constexpr std::size_t kMaxStages = 8;

	// Off the audio thread. Prepares meters and every registered stage.
	void prepare(double sampleRate, std::uint32_t numChannels);
	void reset() noexcept;

	// Off the audio thread, before prepare(). The engine does not take
	// ownership; the stage must outlive the engine. Returns false when full.
	bool addStage(AudioProcessor *stage) noexcept;

	// Audio thread. Processes the block in place.
	void process(AudioBlock &block) noexcept;

	LevelMeter &inputMeter() noexcept { return inputMeter_; }
	LevelMeter &outputMeter() noexcept { return outputMeter_; }

	double sampleRate() const noexcept { return sampleRate_; }
	std::uint32_t numChannels() const noexcept { return numChannels_; }
	std::size_t stageCount() const noexcept { return stageCount_; }

private:
	double sampleRate_ = 48000.0;
	std::uint32_t numChannels_ = 2;

	std::array<AudioProcessor *, kMaxStages> stages_{};
	std::size_t stageCount_ = 0;

	LevelMeter inputMeter_;
	LevelMeter outputMeter_;
};

} // namespace nvoice::engine
