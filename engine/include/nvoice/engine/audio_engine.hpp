// N Voice for Studio - Audio Engine (OBS-independent)
// One AudioEngine instance per OBS filter instance. Owns the DSP chain and
// the input/output meters.
//
// Phase 1: the chain is empty, so the engine is a metered pass-through.
#pragma once

#include "audio_block.hpp"
#include "level_meter.hpp"
#include "processor.hpp"
#include "voice_analyzer.hpp"
#include "waveform_capture.hpp"

#include <array>
#include <atomic>
#include <cstddef>

namespace nvoice::engine {

class AudioEngine {
public:
	static constexpr std::size_t kMaxStages = 8;

	// Off the audio thread. Prepares meters and every registered stage.
	void prepare(double sampleRate, std::uint32_t numChannels);
	void reset() noexcept;

	// Off the audio thread, before prepare(). The engine does not take
	// ownership; the stage must outlive the engine. Returns false when full.
	bool addStage(AudioProcessor *stage) noexcept;

	// Audio thread. Processes the block in place.
	void process(AudioBlock &block) noexcept;

	LevelMeter &inputMeter() noexcept { return inputMeter_; }
	LevelMeter &outputMeter() noexcept { return outputMeter_; }

	// Waveform capture of the input and the processed output. One switch for
	// both so their buckets always line up. Off by default (requirements
	// §4.7); metering and processing run either way.
	void setWaveformEnabled(bool enabled) noexcept { waveformEnabled_.store(enabled, std::memory_order_relaxed); }
	bool waveformEnabled() const noexcept { return waveformEnabled_.load(std::memory_order_relaxed); }
	WaveformCapture &inputWaveform() noexcept { return inputWave_; }
	WaveformCapture &outputWaveform() noexcept { return outputWave_; }

	// Voice measurement for auto setup. Taps the input before processing.
	void setAnalysisEnabled(bool enabled) noexcept { analysisEnabled_.store(enabled, std::memory_order_relaxed); }
	bool analysisEnabled() const noexcept { return analysisEnabled_.load(std::memory_order_relaxed); }
	AnalysisCapture &analysis() noexcept { return analysis_; }

	double sampleRate() const noexcept { return sampleRate_; }
	std::uint32_t numChannels() const noexcept { return numChannels_; }
	std::size_t stageCount() const noexcept { return stageCount_; }

private:
	double sampleRate_ = 48000.0;
	std::uint32_t numChannels_ = 2;

	std::array<AudioProcessor *, kMaxStages> stages_{};
	std::size_t stageCount_ = 0;

	LevelMeter inputMeter_;
	LevelMeter outputMeter_;

	WaveformCapture inputWave_;
	WaveformCapture outputWave_;
	std::atomic<bool> waveformEnabled_{false};
	bool waveformWasEnabled_ = false; // audio thread

	AnalysisCapture analysis_;
	std::atomic<bool> analysisEnabled_{false};
	bool analysisWasEnabled_ = false; // audio thread
};

} // namespace nvoice::engine
