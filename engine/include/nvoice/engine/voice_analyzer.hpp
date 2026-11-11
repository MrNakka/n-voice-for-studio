// N Voice for Studio - Audio Engine (OBS-independent)
// Auto setup ("声に合わせて自動設定"): measures a short sample of the user's
// voice and proposes leveler / compressor settings.
//
// The proposal is only a suggestion; the UI shows it and applies it after
// the user confirms (requirements §2.6).
#pragma once

#include "audio_block.hpp"
#include "spsc_ring_buffer.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace nvoice::engine {

// Level of one short analysis frame (default 50 ms).
struct FrameStats {
	float meanSquare; // mean of squared samples across channels
	float peak;       // highest absolute sample
};

// Audio-thread side: folds the input into frames and hands them to the UI.
class AnalysisCapture {
public:
	static constexpr double kFrameSec = 0.05;

	explicit AnalysisCapture(std::size_t capacity = 512) : ring_(capacity) {}

	void prepare(double sampleRate) noexcept;
	void restart() noexcept; // audio thread
	void process(const AudioBlock &block) noexcept;
	std::size_t read(FrameStats *out, std::size_t maxCount) noexcept { return ring_.pop(out, maxCount); }

private:
	SpscRingBuffer<FrameStats> ring_;
	std::uint32_t frameLength_ = 2400;
	std::uint32_t count_ = 0;
	double sumSquares_ = 0.0;
	std::uint32_t samples_ = 0;
	float peak_ = 0.0f;
};

struct VoiceStats {
	// RMS levels (50 ms frames)
	float noiseDb;       // room noise between words (10th percentile)
	float speechDb;      // typical speaking level (median of speech frames)
	float quietSpeechDb; // quieter speech (25th percentile of speech frames)
	float loudSpeechDb;  // louder speech (90th percentile of speech frames)
	// Peak levels: what the OBS meter shows while speaking
	float speechPeakDb;      // typical (median)
	float quietSpeechPeakDb; // quieter speech (25th percentile)
	float loudSpeechPeakDb;  // louder speech (90th percentile)
	float peakDb;            // highest sample peak
	float speechSeconds;     // how long the user actually spoke
	float totalSeconds;
};

enum CalibrationIssue : unsigned {
	kIssueNone = 0,
	kIssueNotEnoughSpeech = 1u << 0, // retry: too little speech to measure
	kIssueTooQuiet = 1u << 1,        // raise the mic's own input level
	kIssueClipping = 1u << 2,        // peaks near 0 dBFS: lower the mic level
	kIssueNoisy = 1u << 3,           // loud room noise: noise removal would help
};

struct CalibrationProposal {
	float gateDb;
	float maxGainDb;
	float maxCutDb;
	float compStrength;
};

struct CalibrationResult {
	VoiceStats stats;
	unsigned issues;
	bool usable; // false when there was not enough speech
	CalibrationProposal proposal;
};

// Thresholds used by the analysis (documented in the requirements).
namespace calibration {
inline constexpr float kSpeechAboveNoiseDb = 10.0f; // a frame counts as speech this far above the noise
inline constexpr float kMinSpeechSeconds = 2.0f;
inline constexpr float kGateAboveNoiseDb = 8.0f;
inline constexpr float kGateBelowQuietSpeechDb = 6.0f;
inline constexpr float kTooQuietPeakDb = -40.0f; // typical speech peak on the OBS meter
inline constexpr float kClippingDb = -1.0f;
inline constexpr float kNoisyDb = -45.0f;
inline constexpr float kTypicalRangeDb = 15.0f; // speech range that keeps the base strength
inline constexpr float kStrengthPerDb = 2.5f;
} // namespace calibration

// targetDb: where normal speech should read on the OBS meter
// (levels::kNormalPeakDb); boost and cut are planned against it.
// baseStrength: the current compressor strength, adjusted by the measured range.
CalibrationResult analyzeVoice(std::span<const FrameStats> frames, float targetDb, float baseStrength,
			       double frameSec = AnalysisCapture::kFrameSec);

} // namespace nvoice::engine
