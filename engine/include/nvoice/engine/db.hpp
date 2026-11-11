// N Voice for Studio - Audio Engine (OBS-independent)
// Decibel helpers.
#pragma once

#include <cmath>

namespace nvoice::engine {

// Values at or below this are reported as silence (-inf dBFS in the UI).
inline constexpr float kSilenceDb = -120.0f;

inline float linearToDb(float linear) noexcept
{
	if (!(linear > 0.0f))
		return kSilenceDb;
	const float db = 20.0f * std::log10(linear);
	return db < kSilenceDb ? kSilenceDb : db;
}

inline float dbToLinear(float db) noexcept
{
	if (db <= kSilenceDb)
		return 0.0f;
	return std::pow(10.0f, db / 20.0f);
}

} // namespace nvoice::engine
