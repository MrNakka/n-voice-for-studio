// N Voice for Studio - OBS integration
// Audio filter that runs the N Voice engine on a source (typically a mic).
#pragma once

namespace nvoice::obs {

inline constexpr const char *kFilterId = "nvoice_studio_filter";

void registerAudioFilter();

} // namespace nvoice::obs
