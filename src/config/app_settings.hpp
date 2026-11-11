// N Voice for Studio - Configuration
// UI-wide settings (theme, waveform visibility, selected preset).
// Per-filter audio settings are stored separately by OBS with each filter.
#pragma once

#include <string_view>

namespace nvoice::config {

inline constexpr int kSettingsVersion = 1;

enum class Theme { Dark, Light };
enum class Profile { Chat, Game, Singing, LateNight };

struct AppSettings {
	Theme theme = Theme::Dark;
	bool waveformVisible = false;
	bool advancedSettings = false;
	Profile profile = Profile::Chat;
};

std::string_view toString(Theme theme);
std::string_view toString(Profile profile);
Theme themeFromString(std::string_view s);     // unknown -> Dark
Profile profileFromString(std::string_view s); // unknown -> Chat

// Loads settings.json from the module config folder. Missing, unreadable or
// corrupt files fall back to defaults (a .bak copy is tried first).
// A file written by a newer plugin version is read best-effort and marked
// read-only so it is not downgraded.
class SettingsStore {
public:
	AppSettings load();
	bool save(const AppSettings &settings);

	bool isReadOnly() const { return readOnly_; }

private:
	bool readOnly_ = false;
};

} // namespace nvoice::config
