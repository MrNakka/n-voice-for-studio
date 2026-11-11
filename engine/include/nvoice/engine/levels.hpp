// N Voice for Studio - Audio Engine (OBS-independent)
// Output level policy, shared by every stream style (requirements §4.0.1).
//
// Expressed in terms of OBS Studio's own mixer meter so that what the user
// sees in OBS matches the goal:
//  - Normal speech: the meter reaches the start of the red zone (-9 dBFS).
//  - Louder speech: stays inside the red zone, never beyond -1 dBFS
//    (OBS shows white / clipping above -0.5 dBFS).
//
// OBS meter (frontend/components/VolumeMeter.cpp): sample peak, immediate
// attack, linear-in-dB decay; default decay "Fast" = 23.53 dB/s
// (OBSBasic.cpp sets MeterDecayRate to VOLUME_METER_DECAY_FAST).
#pragma once

namespace nvoice::engine::levels {

// OBS meter zone boundaries (VolumeMeter.cpp: warningLevel / errorLevel).
inline constexpr float kObsYellowDb = -20.0f;
inline constexpr float kObsRedDb = -9.0f;
inline constexpr float kObsClipDb = -0.5f;
inline constexpr double kObsMeterDecayDbPerSec = 23.53;

// Normal speech should read here on the OBS meter.
inline constexpr float kNormalPeakDb = kObsRedDb;
// Nothing leaves the filter above this (safety limiter ceiling).
inline constexpr float kCeilingDb = -1.0f;

// Target for the auto leveler's internal meter average. The leveler averages
// a per-sample OBS-style meter in dB; OBS draws one peak per 1024-sample tick
// with decay, which reads about 1 dB lower for speech. Verified by emulating
// the OBS meter on the processed output (median reading of steady normal
// speech = kNormalPeakDb for every stream style).
inline constexpr float kLevelerReadingCorrectionDb = 1.0f;
inline constexpr float kLevelerTargetDb = kNormalPeakDb + kLevelerReadingCorrectionDb;

} // namespace nvoice::engine::levels
