// N Voice for Studio - Audio Engine (OBS-independent)
// Single source of truth for every user-facing parameter: id, unit, range,
// default. UI, settings storage and DSP all read from this table.
//
// NOTE: Ranges and defaults below are provisional (requirements v0.2 §7.2,
// §12) and will be tuned in Phase 2.
#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace nvoice::engine {

enum class Unit { Decibel, Ratio, Milliseconds, Percent };

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

// Compressor (Phase 2)
inline constexpr ParameterSpec kCompThreshold{"compressor.threshold", Unit::Decibel, -60.0f, 0.0f, -18.0f};
inline constexpr ParameterSpec kCompRatio{"compressor.ratio", Unit::Ratio, 1.0f, 20.0f, 3.0f};
inline constexpr ParameterSpec kCompAttack{"compressor.attack", Unit::Milliseconds, 0.1f, 200.0f, 10.0f};
inline constexpr ParameterSpec kCompRelease{"compressor.release", Unit::Milliseconds, 10.0f, 2000.0f, 120.0f};
inline constexpr ParameterSpec kCompMakeup{"compressor.makeup_gain", Unit::Decibel, 0.0f, 24.0f, 2.0f};

// Auto leveler (Phase 2)
inline constexpr ParameterSpec kLevelerTarget{"auto_leveler.target_loudness", Unit::Decibel, -40.0f, -6.0f, -20.0f};
inline constexpr ParameterSpec kLevelerMaxGain{"auto_leveler.max_gain", Unit::Decibel, 0.0f, 30.0f, 12.0f};

// Ducking (Phase 3)
inline constexpr ParameterSpec kDuckBgmDepth{"ducking.bgm_depth", Unit::Decibel, -40.0f, 0.0f, -8.0f};
inline constexpr ParameterSpec kDuckGameDepth{"ducking.game_depth", Unit::Decibel, -40.0f, 0.0f, -4.0f};

inline constexpr std::array kAll{
	&kCompThreshold, &kCompRatio,      &kCompAttack,   &kCompRelease,   &kCompMakeup,
	&kLevelerTarget, &kLevelerMaxGain, &kDuckBgmDepth, &kDuckGameDepth,
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

} // namespace nvoice::engine
