#include "filter_settings.hpp"

#include <nvoice/engine/levels.hpp>

namespace nvoice::obs {

namespace p = engine::params;

void setFilterDefaults(obs_data_t *settings)
{
	// Both voice features start on: adding the filter is the user's request
	// to process this source (the dashboard shows and toggles them).
	obs_data_set_default_bool(settings, kCompressorEnabled, true);
	obs_data_set_default_bool(settings, kLevelerEnabled, true);
	for (const auto *spec : p::kAll)
		obs_data_set_default_double(settings, spec->id.data(), spec->defaultValue);
}

void migrateFilterSettings(obs_data_t *settings)
{
	const long long version = obs_data_get_int(settings, kSettingsVersion);
	if (version >= kCurrentSettingsVersion)
		return;
	if (version < 2) {
		const auto tr = engine::compressorFromStrength(getParam(settings, p::kCompStrength));
		setParam(settings, p::kCompThreshold, tr.threshold);
		setParam(settings, p::kCompRatio, tr.ratio);
		// No longer user settings: target is fixed, makeup is automatic.
		obs_data_erase(settings, p::kLevelerTarget.id.data());
		obs_data_erase(settings, p::kCompMakeup.id.data());
	}
	obs_data_set_int(settings, kSettingsVersion, kCurrentSettingsVersion);
}

float getParam(obs_data_t *settings, const engine::ParameterSpec &spec)
{
	return spec.clamp(static_cast<float>(obs_data_get_double(settings, spec.id.data())));
}

void setParam(obs_data_t *settings, const engine::ParameterSpec &spec, double value)
{
	obs_data_set_double(settings, spec.id.data(), spec.clamp(static_cast<float>(value)));
}

void applyFilterSettings(obs_data_t *settings, engine::VoiceChain &chain)
{
	const float threshold = getParam(settings, p::kCompThreshold);
	const float ratio = getParam(settings, p::kCompRatio);
	chain.compressor.setSettings({
		threshold,
		ratio,
		getParam(settings, p::kCompAttack),
		getParam(settings, p::kCompRelease),
		engine::Compressor::autoMakeupDb(threshold, ratio, engine::levels::kNormalPeakDb),
	});
	chain.compressor.setEnabled(obs_data_get_bool(settings, kCompressorEnabled));

	chain.leveler.setSettings({
		engine::levels::kLevelerTargetDb,
		getParam(settings, p::kLevelerMaxGain),
		getParam(settings, p::kLevelerMaxCut),
		getParam(settings, p::kLevelerSpeed),
		getParam(settings, p::kLevelerGate),
	});
	chain.leveler.setEnabled(obs_data_get_bool(settings, kLevelerEnabled));
}

} // namespace nvoice::obs
