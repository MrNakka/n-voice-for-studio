// N Voice for Studio - Audio Engine (OBS-independent)
// The full voice processing chain used by one filter instance.
//
// Order: input meter -> auto leveler -> compressor -> peak limiter ->
// output meter.
// The leveler brings normal speech to -9 dBFS on the OBS meter; the
// compressor evens out louder / quieter words around that level; the
// limiter guarantees nothing goes above -1 dBFS (see levels.hpp).
#pragma once

#include "audio_engine.hpp"
#include "auto_leveler.hpp"
#include "compressor.hpp"
#include "peak_limiter.hpp"

namespace nvoice::engine {

struct VoiceChain {
	AutoLeveler leveler;
	Compressor compressor;
	PeakLimiter limiter; // always on: the shared safety ceiling
	AudioEngine engine;

	VoiceChain()
	{
		engine.addStage(&leveler);
		engine.addStage(&compressor);
		engine.addStage(&limiter);
	}

	VoiceChain(const VoiceChain &) = delete;
	VoiceChain &operator=(const VoiceChain &) = delete;
};

} // namespace nvoice::engine
