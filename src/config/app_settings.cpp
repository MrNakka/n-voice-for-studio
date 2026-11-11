#include "app_settings.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>
#include <util/platform.h>

#include <memory>
#include <string>

namespace nvoice::config {
namespace {

constexpr const char *kFileName = "settings.json";

struct BFreeDeleter {
	void operator()(char *p) const { bfree(p); }
};
using BString = std::unique_ptr<char, BFreeDeleter>;

std::string settingsPath()
{
	BString path(obs_module_config_path(kFileName));
	return path ? std::string(path.get()) : std::string();
}

bool ensureConfigDir()
{
	BString dir(obs_module_config_path(""));
	if (!dir)
		return false;
	return os_mkdirs(dir.get()) != MKDIR_ERROR;
}

// Applies migrations from `fromVersion` up to kSettingsVersion in place.
void migrate(obs_data_t *data, long long fromVersion)
{
	// v1 is the first schema; nothing to migrate yet. Future example:
	// if (fromVersion < 2) { rename keys ... }
	(void)data;
	(void)fromVersion;
}

} // namespace

std::string_view toString(Theme theme)
{
	return theme == Theme::Light ? "light" : "dark";
}

std::string_view toString(Profile profile)
{
	switch (profile) {
	case Profile::Game:
		return "game";
	case Profile::Singing:
		return "singing";
	case Profile::LateNight:
		return "late_night";
	case Profile::Chat:
	default:
		return "chat";
	}
}

Theme themeFromString(std::string_view s)
{
	return s == "light" ? Theme::Light : Theme::Dark;
}

Profile profileFromString(std::string_view s)
{
	if (s == "game")
		return Profile::Game;
	if (s == "singing")
		return Profile::Singing;
	if (s == "late_night")
		return Profile::LateNight;
	return Profile::Chat;
}

AppSettings SettingsStore::load()
{
	AppSettings settings;
	readOnly_ = false;

	const std::string path = settingsPath();
	if (path.empty() || !os_file_exists(path.c_str()))
		return settings;

	OBSDataAutoRelease data = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data) {
		obs_log(LOG_WARNING, "settings: could not parse %s, using defaults", path.c_str());
		return settings;
	}

	const long long version = obs_data_get_int(data, "version");
	if (version > kSettingsVersion) {
		obs_log(LOG_WARNING, "settings: file version %lld is newer than supported %d; not overwriting", version,
			kSettingsVersion);
		readOnly_ = true;
	} else if (version < kSettingsVersion) {
		migrate(data, version);
	}

	OBSDataAutoRelease ui = obs_data_get_obj(data, "ui");
	if (ui) {
		settings.theme = themeFromString(obs_data_get_string(ui, "theme"));
		settings.waveformVisible = obs_data_get_bool(ui, "waveform_visible");
		settings.advancedSettings = obs_data_get_bool(ui, "advanced_settings");
	}
	settings.profile = profileFromString(obs_data_get_string(data, "profile"));
	return settings;
}

bool SettingsStore::save(const AppSettings &settings)
{
	if (readOnly_)
		return false;
	if (!ensureConfigDir()) {
		obs_log(LOG_WARNING, "settings: could not create config directory");
		return false;
	}

	OBSDataAutoRelease ui = obs_data_create();
	obs_data_set_string(ui, "theme", std::string(toString(settings.theme)).c_str());
	obs_data_set_bool(ui, "waveform_visible", settings.waveformVisible);
	obs_data_set_bool(ui, "advanced_settings", settings.advancedSettings);

	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_int(data, "version", kSettingsVersion);
	obs_data_set_obj(data, "ui", ui);
	obs_data_set_string(data, "profile", std::string(toString(settings.profile)).c_str());

	const std::string path = settingsPath();
	const bool ok = obs_data_save_json_pretty_safe(data, path.c_str(), "tmp", "bak");
	if (!ok)
		obs_log(LOG_WARNING, "settings: failed to write %s", path.c_str());
	return ok;
}

} // namespace nvoice::config
