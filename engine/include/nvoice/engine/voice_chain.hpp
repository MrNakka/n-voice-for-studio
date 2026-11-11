// N Voice for Studio - Audio Engine (OBS-independent)
// The full voice processing chain used by one filter instance.
//
// Order: input meter -> auto leveler -> compressor -> output meter.
// The leveler first brings speech to a steady level; the compressor then
// evens out what is left (sudden loud words) on top of that level.
#pragma once

#include "audio_engine.hpp"
#include "auto_leveler.hpp"
#include "compressor.hpp"

namespace nvoice::engine {

struct VoiceChain {
	AutoLeveler leveler;
	Compressor compressor;
	AudioEngine engine;

	VoiceChain()
	{
		engine.addStage(&leveler);
		engine.addStage(&compressor);
	}

	VoiceChain(const VoiceChain &) = delete;
	VoiceChain &operator=(const VoiceChain &) = delete;
};

} // namespace nvoice::engine
