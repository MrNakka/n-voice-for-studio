#include "filter_settings.hpp"

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
	chain.compressor.setSettings({
		getParam(settings, p::kCompThreshold),
		getParam(settings, p::kCompRatio),
		getParam(settings, p::kCompAttack),
		getParam(settings, p::kCompRelease),
		getParam(settings, p::kCompMakeup),
	});
	chain.compressor.setEnabled(obs_data_get_bool(settings, kCompressorEnabled));

	chain.leveler.setSettings({
		getParam(settings, p::kLevelerTarget),
		getParam(settings, p::kLevelerMaxGain),
		getParam(settings, p::kLevelerMaxCut),
		getParam(settings, p::kLevelerSpeed),
		getParam(settings, p::kLevelerGate),
	});
	chain.leveler.setEnabled(obs_data_get_bool(settings, kLevelerEnabled));
}

} // namespace nvoice::obs
