// N Voice for Studio - OBS integration
// Per-filter settings: stored by OBS with each filter instance (and so with
// the scene collection), keyed by the ids in engine/parameters.hpp.
#pragma once

#include <nvoice/engine/parameters.hpp>
#include <nvoice/engine/voice_chain.hpp>

#include <obs.h>

namespace nvoice::obs {

inline constexpr const char *kCompressorEnabled = "compressor.enabled";
inline constexpr const char *kLevelerEnabled = "auto_leveler.enabled";

// Parameters that belong to each feature (used for reset and presets).
inline constexpr std::array kCompressorParams{
	&engine::params::kCompStrength, &engine::params::kCompThreshold, &engine::params::kCompRatio,
	&engine::params::kCompAttack,   &engine::params::kCompRelease,   &engine::params::kCompMakeup,
};
inline constexpr std::array kLevelerParams{
	&engine::params::kLevelerTarget, &engine::params::kLevelerMaxGain, &engine::params::kLevelerMaxCut,
	&engine::params::kLevelerSpeed,  &engine::params::kLevelerGate,
};

void setFilterDefaults(obs_data_t *settings);

// Reads every value (clamped to its range) and pushes it into the chain.
void applyFilterSettings(obs_data_t *settings, engine::VoiceChain &chain);

float getParam(obs_data_t *settings, const engine::ParameterSpec &spec);
void setParam(obs_data_t *settings, const engine::ParameterSpec &spec, double value);

} // namespace nvoice::obs
