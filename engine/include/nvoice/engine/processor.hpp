// N Voice for Studio - Audio Engine (OBS-independent)
// Interface implemented by every DSP stage (compressor, auto leveler, ...).
#pragma once

#include "audio_block.hpp"

namespace nvoice::engine {

class AudioProcessor {
public:
	virtual ~AudioProcessor() = default;

	// Called off the audio thread before processing starts or after a format
	// change. Allocate everything the stage needs here.
	virtual void prepare(double sampleRate, std::uint32_t numChannels) = 0;

	// Clears internal state (envelopes, delay lines) without reallocating.
	virtual void reset() noexcept = 0;

	// Audio thread. Processes the block in place. Must not allocate, lock,
	// block, or perform I/O.
	virtual void process(AudioBlock &block) noexcept = 0;
};

} // namespace nvoice::engine
