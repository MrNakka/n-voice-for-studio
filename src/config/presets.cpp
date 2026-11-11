#include "presets.hpp"

namespace nvoice::config {

namespace {

// Chat: natural talking voice, moderate evening-out.
constexpr PresetValues kChat{30.0f, 10.0f, 150.0f, 12.0f, 12.0f, 3.0f, -50.0f};

// Game: quicker and firmer so shouts during play are caught; a higher gate
// keeps game audio bleeding into the mic from being treated as speech.
constexpr PresetValues kGame{45.0f, 5.0f, 120.0f, 9.0f, 12.0f, 2.0f, -45.0f};

// Singing: gentle and slow to keep the dynamics of the performance.
constexpr PresetValues kSinging{15.0f, 20.0f, 250.0f, 6.0f, 6.0f, 6.0f, -50.0f};

// Late night: quiet speech, so more lift and firmer compression.
constexpr PresetValues kLateNight{60.0f, 5.0f, 150.0f, 18.0f, 12.0f, 2.0f, -55.0f};

} // namespace

const PresetValues &presetFor(Profile profile)
{
	switch (profile) {
	case Profile::Game:
		return kGame;
	case Profile::Singing:
		return kSinging;
	case Profile::LateNight:
		return kLateNight;
	case Profile::Chat:
	default:
		return kChat;
	}
}

} // namespace nvoice::config
