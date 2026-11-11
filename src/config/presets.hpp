// N Voice for Studio - Configuration
// Stream-style presets (requirements §4.9). Values are starting points:
// results depend on the microphone and the room.
//
// The output level is NOT part of a style: every style aims for the same
// OBS meter reading (normal speech at -9 dBFS, never above -1 dBFS; see
// engine/levels.hpp). Styles differ in how much quiet speech is lifted, how
// the compressor feels, and (Phase 3) how much BGM is ducked.
#pragma once

#include "app_settings.hpp"

namespace nvoice::config {

struct PresetValues {
	// Compressor (threshold / ratio come from strength)
	float compStrength;
	float compAttackMs;
	float compReleaseMs;
	// Auto leveler
	float levelerMaxGainDb;
	float levelerMaxCutDb;
	float levelerSpeedSec;
	float levelerGateDb;
};

const PresetValues &presetFor(Profile profile);

} // namespace nvoice::config
