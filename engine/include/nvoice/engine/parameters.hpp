// N Voice for Studio - Audio Engine (OBS-independent)
// Single source of truth for every user-facing parameter: id, unit, range,
// default. UI, settings storage and DSP all read from this table.
//
// Output levels shared by every style live in levels.hpp.
//
// NOTE: Ranges and defaults are starting points (requirements §4.9: presets
// are a starting point, results depend on the mic and room).
#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace nvoice::engine {

enum class Unit { Decibel, Ratio, Milliseconds, Seconds, Percent };

struct ParameterSpec {
	std::string_view id; // stable key used in settings JSON
	Unit unit;
	float min;
	float max;
	float defaultValue;

	constexpr float clamp(float v) const noexcept
	{
		// NaN falls back to the default rather than propagating.
		if (!(v == v))
			return defaultValue;
		return std::clamp(v, min, max);
	}
};

namespace params {

// Compressor
// "strength" is the beginner control; it drives threshold and ratio (see
// compressorFromStrength). Makeup gain is not a user setting: it is derived
// so normal speech stays at levels::kNormalPeakDb (autoMakeupDb).
inline constexpr ParameterSpec kCompStrength{"compressor.strength", Unit::Percent, 0.0f, 100.0f, 30.0f};
inline constexpr ParameterSpec kCompThreshold{"compressor.threshold", Unit::Decibel, -60.0f, 0.0f, -12.6f};
inline constexpr ParameterSpec kCompRatio{"compressor.ratio", Unit::Ratio, 1.0f, 20.0f, 2.25f};
inline constexpr ParameterSpec kCompAttack{"compressor.attack", Unit::Milliseconds, 0.1f, 200.0f, 10.0f};
inline constexpr ParameterSpec kCompRelease{"compressor.release", Unit::Milliseconds, 10.0f, 2000.0f, 150.0f};
// Internal: computed from threshold / ratio, never stored as a user value.
inline constexpr ParameterSpec kCompMakeup{"compressor.makeup_gain", Unit::Decibel, 0.0f, 24.0f, 0.0f};

// Auto leveler
// Internal: the target is the shared output level (levels::kLevelerTargetDb),
// the same for every stream style.
inline constexpr ParameterSpec kLevelerTarget{"auto_leveler.target_loudness", Unit::Decibel, -40.0f, -3.0f, -8.0f};
inline constexpr ParameterSpec kLevelerMaxGain{"auto_leveler.max_gain", Unit::Decibel, 0.0f, 30.0f, 12.0f};
inline constexpr ParameterSpec kLevelerMaxCut{"auto_leveler.max_cut", Unit::Decibel, 0.0f, 30.0f, 12.0f};
inline constexpr ParameterSpec kLevelerSpeed{"auto_leveler.speed", Unit::Seconds, 0.5f, 10.0f, 3.0f};
inline constexpr ParameterSpec kLevelerGate{"auto_leveler.gate", Unit::Decibel, -80.0f, -30.0f, -50.0f};

// Ducking (Phase 3)
inline constexpr ParameterSpec kDuckBgmDepth{"ducking.bgm_depth", Unit::Decibel, -40.0f, 0.0f, -8.0f};
inline constexpr ParameterSpec kDuckGameDepth{"ducking.game_depth", Unit::Decibel, -40.0f, 0.0f, -4.0f};

// User-facing parameters (stored per filter, shown in the UI).
inline constexpr std::array kAll{
	&kCompStrength,  &kCompThreshold, &kCompRatio,   &kCompAttack,   &kCompRelease,   &kLevelerMaxGain,
	&kLevelerMaxCut, &kLevelerSpeed,  &kLevelerGate, &kDuckBgmDepth, &kDuckGameDepth,
};

} // namespace params

inline constexpr const ParameterSpec *findParameter(std::string_view id) noexcept
{
	for (const auto *spec : params::kAll) {
		if (spec->id == id)
			return spec;
	}
	return nullptr;
}

// Beginner "strength" (0-100 %) to compressor threshold / ratio, anchored to
// the normal speech level (-9 dBFS on the OBS meter):
// 0 % = only louder-than-normal speech is touched (-9 dB, 1.5:1),
// 100 % = strong evening out (-21 dB, 4:1).
struct ThresholdRatio {
	float threshold;
	float ratio;
};

inline constexpr ThresholdRatio compressorFromStrength(float strengthPercent) noexcept
{
	const float s = params::kCompStrength.clamp(strengthPercent);
	return {params::kCompThreshold.clamp(-9.0f - 0.12f * s), params::kCompRatio.clamp(1.5f + 0.025f * s)};
}

} // namespace nvoice::engine
