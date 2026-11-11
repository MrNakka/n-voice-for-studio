#include "nvoice_filter.hpp"

#include "filter_registry.hpp"
#include "filter_settings.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <array>
#include <memory>

namespace nvoice::obs {
namespace {

struct FilterInstance {
	obs_source_t *context = nullptr;
	std::shared_ptr<FilterShared> shared;
	std::uint32_t numChannels = 2;
	std::uint64_t registryId = 0;
};

const char *filterGetName(void *)
{
	return obs_module_text("NVoice.Filter.Name");
}

void *filterCreate(obs_data_t *settings, obs_source_t *source)
{
	auto *instance = new FilterInstance();
	instance->context = source;
	instance->shared = std::make_shared<FilterShared>();

	// Sample rate and channel layout come from OBS's global audio output.
	// Changing them in OBS requires a restart, so reading once is enough.
	audio_t *audio = obs_get_audio();
	const double sampleRate = audio ? static_cast<double>(audio_output_get_sample_rate(audio)) : 48000.0;
	const std::size_t channels = audio ? audio_output_get_channels(audio) : 2;
	instance->numChannels = static_cast<std::uint32_t>(
		channels > engine::kMaxChannels ? engine::kMaxChannels : (channels == 0 ? 1 : channels));

	instance->shared->chain.engine.prepare(sampleRate, instance->numChannels);
	migrateFilterSettings(settings);
	applyFilterSettings(settings, instance->shared->chain);
	instance->registryId = FilterRegistry::instance().add(source, instance->shared);

	obs_log(LOG_INFO, "filter created (%.0f Hz, %u ch)", sampleRate, instance->numChannels);
	return instance;
}

void filterDestroy(void *data)
{
	auto *instance = static_cast<FilterInstance *>(data);
	FilterRegistry::instance().remove(instance->registryId);
	delete instance;
}

// Audio thread. Must not allocate, lock, block, or log.
obs_audio_data *filterAudio(void *data, obs_audio_data *audio)
{
	auto *instance = static_cast<FilterInstance *>(data);
	if (audio == nullptr || audio->frames == 0)
		return audio;

	std::array<float *, engine::kMaxChannels> planes{};
	for (std::uint32_t c = 0; c < instance->numChannels; ++c)
		planes[c] = reinterpret_cast<float *>(audio->data[c]);

	engine::AudioBlock block{planes.data(), instance->numChannels, audio->frames};
	instance->shared->chain.engine.process(block);
	return audio;
}

// Any thread (UI, OBS settings dialog). The chain takes values atomically.
void filterUpdate(void *data, obs_data_t *settings)
{
	applyFilterSettings(settings, static_cast<FilterInstance *>(data)->shared->chain);
}

void filterGetDefaults(obs_data_t *settings)
{
	setFilterDefaults(settings);
}

obs_properties_t *filterGetProperties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_text(props, "nvoice_info", obs_module_text("NVoice.Filter.Info"), OBS_TEXT_INFO);
	return props;
}

} // namespace

void registerAudioFilter()
{
	obs_source_info info{};
	info.id = kFilterId;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_AUDIO;
	info.get_name = filterGetName;
	info.create = filterCreate;
	info.destroy = filterDestroy;
	info.filter_audio = filterAudio;
	info.get_properties = filterGetProperties;
	info.get_defaults = filterGetDefaults;
	info.update = filterUpdate;
	obs_register_source(&info);
}

} // namespace nvoice::obs
