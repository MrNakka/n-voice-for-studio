// N Voice for Studio - Audio Engine (OBS-independent)
// Planar float audio block view passed through the processing pipeline.
#pragma once

#include <cstdint>

namespace nvoice::engine {

// Maximum channel count the engine handles. Matches OBS's MAX_AUDIO_CHANNELS (8).
inline constexpr std::uint32_t kMaxChannels = 8;

// Non-owning view over planar float32 audio.
// channels[c] points to numFrames samples for channel c. A null plane is skipped.
// The engine never assumes a fixed numFrames: OBS may deliver any buffer size.
struct AudioBlock {
	float *const *channels = nullptr;
	std::uint32_t numChannels = 0;
	std::uint32_t numFrames = 0;
};

} // namespace nvoice::engine
