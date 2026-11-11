#include "nvoice/engine/voice_analyzer.hpp"

#include "nvoice/engine/db.hpp"
#include "nvoice/engine/parameters.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nvoice::engine {

// ---------------------------------------------------------------------------
// AnalysisCapture
// ---------------------------------------------------------------------------

void AnalysisCapture::prepare(double sampleRate) noexcept
{
	const double frames = std::round((sampleRate > 0.0 ? sampleRate : 48000.0) * kFrameSec);
	frameLength_ = static_cast<std::uint32_t>(std::max(1.0, frames));
	restart();
}

void AnalysisCapture::restart() noexcept
{
	count_ = 0;
	sumSquares_ = 0.0;
	samples_ = 0;
	peak_ = 0.0f;
}

void AnalysisCapture::process(const AudioBlock &block) noexcept
{
	if (block.channels == nullptr || block.numFrames == 0)
		return;
	const std::uint32_t channels = std::min(block.numChannels, kMaxChannels);

	for (std::uint32_t i = 0; i < block.numFrames; ++i) {
		for (std::uint32_t c = 0; c < channels; ++c) {
			if (const float *ch = block.channels[c]) {
				const float s = ch[i];
				sumSquares_ += static_cast<double>(s) * s;
				peak_ = std::max(peak_, std::fabs(s));
				++samples_;
			}
		}
		if (++count_ == frameLength_) {
			const FrameStats frame{
				samples_ > 0 ? static_cast<float>(sumSquares_ / samples_) : 0.0f,
				peak_,
			};
			ring_.push(&frame, 1);
			restart();
		}
	}
}

// ---------------------------------------------------------------------------
// analyzeVoice
// ---------------------------------------------------------------------------

namespace {

float msToDb(float meanSquare)
{
	return meanSquare > 0.0f ? std::max(kSilenceDb, 10.0f * std::log10(meanSquare)) : kSilenceDb;
}

// Nearest-rank percentile of an already sorted vector (p in 0..1).
float percentile(const std::vector<float> &sorted, float p)
{
	if (sorted.empty())
		return kSilenceDb;
	const auto idx = static_cast<std::size_t>(std::lround(p * static_cast<float>(sorted.size() - 1)));
	return sorted[std::min(idx, sorted.size() - 1)];
}

float roundTo(float value, float step)
{
	return std::round(value / step) * step;
}

} // namespace

CalibrationResult analyzeVoice(std::span<const FrameStats> frames, float targetDb, float baseStrength, double frameSec)
{
	namespace c = calibration;
	namespace p = params;

	CalibrationResult result{};
	result.stats.totalSeconds = static_cast<float>(frames.size() * frameSec);

	std::vector<float> levels;
	levels.reserve(frames.size());
	float peak = 0.0f;
	for (const auto &f : frames) {
		levels.push_back(msToDb(f.meanSquare));
		peak = std::max(peak, f.peak);
	}
	std::vector<float> sorted = levels;
	std::sort(sorted.begin(), sorted.end());

	const float noise = percentile(sorted, 0.10f);
	const float speechFloor = std::max(noise + c::kSpeechAboveNoiseDb, linearToDb(1e-3f)); // >= -60 dBFS

	std::vector<float> speech;      // RMS of speech frames
	std::vector<float> speechPeaks; // peak of speech frames (OBS meter reading)
	for (std::size_t i = 0; i < levels.size(); ++i) {
		if (levels[i] > speechFloor) {
			speech.push_back(levels[i]);
			speechPeaks.push_back(linearToDb(frames[i].peak));
		}
	}
	std::sort(speech.begin(), speech.end());
	std::sort(speechPeaks.begin(), speechPeaks.end());

	auto &s = result.stats;
	s.noiseDb = noise;
	s.peakDb = linearToDb(peak);
	s.speechSeconds = static_cast<float>(speech.size() * frameSec);
	s.speechDb = percentile(speech, 0.50f);
	s.quietSpeechDb = percentile(speech, 0.25f);
	s.loudSpeechDb = percentile(speech, 0.90f);
	s.speechPeakDb = percentile(speechPeaks, 0.50f);
	s.quietSpeechPeakDb = percentile(speechPeaks, 0.25f);
	s.loudSpeechPeakDb = percentile(speechPeaks, 0.90f);

	unsigned issues = kIssueNone;
	if (s.speechSeconds < c::kMinSpeechSeconds)
		issues |= kIssueNotEnoughSpeech;
	if (s.peakDb > c::kClippingDb)
		issues |= kIssueClipping;
	if (!(issues & kIssueNotEnoughSpeech)) {
		if (s.speechPeakDb < c::kTooQuietPeakDb)
			issues |= kIssueTooQuiet;
		if (noise > c::kNoisyDb)
			issues |= kIssueNoisy;
	}
	result.issues = issues;
	result.usable = !(issues & kIssueNotEnoughSpeech);
	if (!result.usable)
		return result;

	auto &prop = result.proposal;

	// Gate: just above the room noise, but safely below quiet speech so
	// quiet words still move the leveler.
	float gate = std::min(noise + c::kGateAboveNoiseDb, s.quietSpeechDb - c::kGateBelowQuietSpeechDb);
	prop.gateDb = p::kLevelerGate.clamp(roundTo(gate, 1.0f));

	// Max boost: enough to bring quieter speech up to the target reading.
	const float neededBoost = targetDb - s.quietSpeechPeakDb;
	prop.maxGainDb = p::kLevelerMaxGain.clamp(roundTo(std::max(3.0f, neededBoost), 1.0f));

	// Max cut: enough to bring louder speech down to the target reading.
	const float neededCut = s.loudSpeechPeakDb - targetDb;
	prop.maxCutDb = p::kLevelerMaxCut.clamp(roundTo(std::max(3.0f, neededCut + 3.0f), 1.0f));

	// Strength: a wider gap between quiet and loud speech needs more evening out.
	const float range = s.loudSpeechDb - s.quietSpeechDb;
	const float strength = baseStrength + (range - c::kTypicalRangeDb) * c::kStrengthPerDb;
	prop.compStrength = std::clamp(roundTo(strength, 5.0f), 10.0f, 85.0f);

	return result;
}

} // namespace nvoice::engine
