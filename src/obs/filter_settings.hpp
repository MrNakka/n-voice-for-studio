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
// Settings schema of a filter instance (see migrateFilterSettings).
inline constexpr const char *kSettingsVersion = "nvoice.version";
inline constexpr long long kCurrentSettingsVersion = 2;

// Parameters that belong to each feature (used for reset and presets).
inline constexpr std::array kCompressorParams{
	&engine::params::kCompStrength, &engine::params::kCompThreshold, &engine::params::kCompRatio,
	&engine::params::kCompAttack,   &engine::params::kCompRelease,
};
inline constexpr std::array kLevelerParams{
	&engine::params::kLevelerMaxGain,
	&engine::params::kLevelerMaxCut,
	&engine::params::kLevelerSpeed,
	&engine::params::kLevelerGate,
};

void setFilterDefaults(obs_data_t *settings);

// Upgrades settings saved by older versions in place. v1 -> v2: strength now
// maps to a threshold / ratio anchored at the OBS red line (-9 dBFS), and
// makeup gain is automatic, so threshold / ratio are recomputed from strength.
void migrateFilterSettings(obs_data_t *settings);

// Reads every value (clamped to its range) and pushes it into the chain.
// The output level policy (levels.hpp) is applied here: the leveler target
// and the compressor makeup are fixed / derived, not user settings.
void applyFilterSettings(obs_data_t *settings, engine::VoiceChain &chain);

float getParam(obs_data_t *settings, const engine::ParameterSpec &spec);
void setParam(obs_data_t *settings, const engine::ParameterSpec &spec, double value);

} // namespace nvoice::obs
