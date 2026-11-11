// N Voice for Studio - Configuration
// Stream-style presets (requirements §4.9). Values are starting points:
// results depend on the microphone and the room.
#pragma once

#include "app_settings.hpp"

namespace nvoice::config {

struct PresetValues {
	// Compressor (threshold / ratio come from strength)
	float compStrength;
	float compAttackMs;
	float compReleaseMs;
	float compMakeupDb;
	// Auto leveler
	float levelerTargetDb;
	float levelerMaxGainDb;
	float levelerMaxCutDb;
	float levelerSpeedSec;
	float levelerGateDb;
};

const PresetValues &presetFor(Profile profile);

} // namespace nvoice::config
