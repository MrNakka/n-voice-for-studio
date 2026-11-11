// Unit tests for the OBS-independent audio engine.
// Dependency-free on purpose: run with `ctest` or execute the binary directly.

#include "nvoice/engine/audio_engine.hpp"
#include "nvoice/engine/auto_leveler.hpp"
#include "nvoice/engine/compressor.hpp"
#include "nvoice/engine/db.hpp"
#include "nvoice/engine/parameters.hpp"
#include "nvoice/engine/spsc_ring_buffer.hpp"
#include "nvoice/engine/levels.hpp"
#include "nvoice/engine/peak_limiter.hpp"
#include "nvoice/engine/voice_analyzer.hpp"
#include "nvoice/engine/voice_chain.hpp"
#include "nvoice/engine/waveform_capture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                       \
	do {                                                                              \
		++g_checks;                                                               \
		if (!(cond)) {                                                            \
			++g_failures;                                                     \
			std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
		}                                                                         \
	} while (0)

#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

using namespace nvoice::engine;

struct Buffer {
	std::vector<std::vector<float>> planes;
	std::vector<float *> ptrs;

	Buffer(std::uint32_t channels, std::uint32_t frames) : planes(channels, std::vector<float>(frames, 0.0f))
	{
		for (auto &p : planes)
			ptrs.push_back(p.data());
	}

	AudioBlock block()
	{
		return {ptrs.data(), static_cast<std::uint32_t>(ptrs.size()),
			static_cast<std::uint32_t>(planes.empty() ? 0 : planes[0].size())};
	}

	void fillSine(float amplitude, double freq, double sampleRate, std::uint64_t &phaseFrames)
	{
		const auto frames = planes[0].size();
		for (std::size_t i = 0; i < frames; ++i) {
			const double t = static_cast<double>(phaseFrames + i) / sampleRate;
			const float v = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * freq * t));
			for (auto &p : planes)
				p[i] = v;
		}
		phaseFrames += frames;
	}
};

void test_db_conversion()
{
	CHECK_NEAR(linearToDb(1.0f), 0.0f, 1e-5f);
	CHECK_NEAR(linearToDb(0.5f), -6.0206f, 1e-3f);
	CHECK(linearToDb(0.0f) == kSilenceDb);
	CHECK(linearToDb(-1.0f) == kSilenceDb);
	CHECK(linearToDb(std::nanf("")) == kSilenceDb);
	CHECK_NEAR(dbToLinear(-6.0206f), 0.5f, 1e-4f);
	CHECK(dbToLinear(kSilenceDb) == 0.0f);
}

void test_passthrough_is_bit_exact()
{
	AudioEngine engine;
	engine.prepare(48000.0, 2);

	Buffer buf(2, 480);
	std::uint64_t phase = 0;
	buf.fillSine(0.3f, 440.0, 48000.0, phase);
	const auto original = buf.planes;

	auto block = buf.block();
	engine.process(block);

	CHECK(buf.planes == original);
}

void test_meter_sine_levels()
{
	// A full-scale-relative sine at amplitude A has peak A and RMS A/sqrt(2).
	AudioEngine engine;
	engine.prepare(48000.0, 2);

	const float amplitude = 0.5f;
	Buffer buf(2, 512);
	std::uint64_t phase = 0;
	// 2 seconds of audio so the 300 ms RMS smoother settles.
	for (int i = 0; i < 188; ++i) {
		buf.fillSine(amplitude, 1000.0, 48000.0, phase);
		auto block = buf.block();
		engine.process(block);
	}

	const auto reading = engine.inputMeter().read();
	CHECK_NEAR(reading.peakDb, linearToDb(amplitude), 0.05f);
	CHECK_NEAR(reading.rmsDb, linearToDb(amplitude / std::numbers::sqrt2_v<float>), 0.1f);

	// Peak accumulator is cleared by read(); RMS is not.
	const auto again = engine.inputMeter().read();
	CHECK(again.peakDb == kSilenceDb);
	CHECK_NEAR(again.rmsDb, reading.rmsDb, 1e-6f);

	// Output meter matches input in pass-through.
	const auto out = engine.outputMeter().peek();
	CHECK_NEAR(out.rmsDb, reading.rmsDb, 1e-4f);
}

void test_meter_variable_block_sizes()
{
	// Same signal delivered in different block sizes must settle to the same RMS.
	auto settle = [](std::uint32_t blockSize) {
		LevelMeter meter;
		meter.prepare(48000.0);
		Buffer buf(1, blockSize);
		std::uint64_t phase = 0;
		const std::uint64_t total = 96000; // 2 s
		while (phase < total) {
			buf.fillSine(0.25f, 500.0, 48000.0, phase);
			meter.process(buf.block());
		}
		return meter.peek().rmsDb;
	};
	const float a = settle(64);
	const float b = settle(1024);
	CHECK_NEAR(a, b, 0.1f);
}

void test_meter_handles_null_planes_and_empty_blocks()
{
	LevelMeter meter;
	meter.prepare(48000.0);

	std::vector<float> left(256, 0.5f);
	float *ptrs[2] = {left.data(), nullptr};
	meter.process({ptrs, 2, 256});
	CHECK_NEAR(meter.peek().peakDb, linearToDb(0.5f), 1e-4f);

	meter.process({ptrs, 2, 0});
	meter.process({nullptr, 2, 256});
	CHECK_NEAR(meter.read().peakDb, linearToDb(0.5f), 1e-4f);
}

class GainStage final : public AudioProcessor {
public:
	explicit GainStage(float gain) : gain_(gain) {}
	void prepare(double sr, std::uint32_t ch) override
	{
		preparedRate = sr;
		preparedChannels = ch;
	}
	void reset() noexcept override { ++resets; }
	void process(AudioBlock &block) noexcept override
	{
		for (std::uint32_t c = 0; c < block.numChannels; ++c)
			for (std::uint32_t i = 0; i < block.numFrames; ++i)
				block.channels[c][i] *= gain_;
	}
	double preparedRate = 0.0;
	std::uint32_t preparedChannels = 0;
	int resets = 0;

private:
	float gain_;
};

void test_stage_chain_order_and_meters()
{
	GainStage half(0.5f);
	GainStage quarter(0.25f);
	AudioEngine engine;
	CHECK(engine.addStage(&half));
	CHECK(engine.addStage(&quarter));
	CHECK(!engine.addStage(nullptr));
	engine.prepare(44100.0, 12); // clamped to kMaxChannels

	CHECK(half.preparedRate == 44100.0);
	CHECK(half.preparedChannels == kMaxChannels);

	Buffer buf(1, 128);
	for (auto &s : buf.planes[0])
		s = 1.0f;
	auto block = buf.block();
	engine.process(block);

	CHECK_NEAR(buf.planes[0][0], 0.125f, 1e-6f);
	CHECK_NEAR(engine.inputMeter().read().peakDb, 0.0f, 1e-4f);
	CHECK_NEAR(engine.outputMeter().read().peakDb, linearToDb(0.125f), 1e-4f);

	engine.reset();
	CHECK(half.resets == 1 && quarter.resets == 1);
}

void test_stage_capacity()
{
	GainStage unity(1.0f);
	AudioEngine engine;
	for (std::size_t i = 0; i < AudioEngine::kMaxStages; ++i)
		CHECK(engine.addStage(&unity));
	CHECK(!engine.addStage(&unity));
	CHECK(engine.stageCount() == AudioEngine::kMaxStages);
}

void test_parameters()
{
	using namespace nvoice::engine::params;
	CHECK(kCompRatio.clamp(100.0f) == kCompRatio.max);
	CHECK(kCompRatio.clamp(0.0f) == kCompRatio.min);
	CHECK(kCompRatio.clamp(std::nanf("")) == kCompRatio.defaultValue);
	CHECK(findParameter("compressor.threshold") == &kCompThreshold);
	CHECK(findParameter("does.not.exist") == nullptr);
	CHECK(params::kLevelerTarget.defaultValue == levels::kLevelerTargetDb);
	CHECK(params::kCompThreshold.defaultValue ==
	      compressorFromStrength(params::kCompStrength.defaultValue).threshold);

	for (const auto *spec : kAll) {
		CHECK(spec->min < spec->max);
		CHECK(spec->defaultValue >= spec->min && spec->defaultValue <= spec->max);
		// ids must be unique
		int count = 0;
		for (const auto *other : kAll)
			count += other->id == spec->id ? 1 : 0;
		CHECK(count == 1);
	}
}

void test_ring_buffer_basic()
{
	SpscRingBuffer<float> rb(4);
	CHECK(rb.capacity() == 4);
	CHECK(rb.availableToWrite() == 4);

	const float in[6] = {1, 2, 3, 4, 5, 6};
	CHECK(rb.push(in, 6) == 4); // only capacity fits
	CHECK(rb.availableToRead() == 4);
	CHECK(rb.availableToWrite() == 0);

	float out[6] = {};
	CHECK(rb.pop(out, 3) == 3);
	CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3);

	CHECK(rb.push(in + 4, 2) == 2); // wraps around
	CHECK(rb.pop(out, 6) == 3);
	CHECK(out[0] == 4 && out[1] == 5 && out[2] == 6);
	CHECK(rb.availableToRead() == 0);
}

void test_ring_buffer_threads()
{
	// Producer and consumer on separate threads must see every value in order.
	constexpr std::uint32_t total = 200000;
	SpscRingBuffer<std::uint32_t> rb(256);
	bool ordered = true;

	std::thread consumer([&] {
		std::uint32_t expected = 0;
		std::uint32_t chunk[64];
		while (expected < total) {
			const auto n = rb.pop(chunk, 64);
			for (std::size_t i = 0; i < n; ++i) {
				if (chunk[i] != expected)
					ordered = false;
				++expected;
			}
			if (n == 0)
				std::this_thread::yield();
		}
	});

	std::uint32_t next = 0;
	std::uint32_t chunk[48];
	while (next < total) {
		std::uint32_t count = 0;
		while (count < 48 && next + count < total) {
			chunk[count] = next + count;
			++count;
		}
		std::size_t written = 0;
		while (written < count) {
			written += rb.push(chunk + written, count - written);
			if (written < count)
				std::this_thread::yield();
		}
		next += count;
	}
	consumer.join();
	CHECK(ordered);
}

// ---------------------------------------------------------------------------
// Phase 2: compressor / auto leveler
// ---------------------------------------------------------------------------

constexpr double kRate = 48000.0;

// Feeds `seconds` of a sine at `rmsDb` (RMS) through `stage` in blocks of
// `blockSize`, and returns output RMS / peak over the final `measureSec`.
struct Measured {
	float rmsDb;
	float peakDb;
};

template<typename Stage>
Measured runSine(Stage &stage, float rmsDb, double seconds, std::uint32_t blockSize = 480, double measureSec = 0.5,
		 std::uint32_t channels = 2)
{
	Buffer buf(channels, blockSize);
	std::uint64_t phase = 0;
	const float amplitude = dbToLinear(rmsDb) * std::numbers::sqrt2_v<float>;
	const auto total = static_cast<std::uint64_t>(seconds * kRate);
	const auto measureFrom = total - static_cast<std::uint64_t>(measureSec * kRate);
	double sumSq = 0.0;
	std::uint64_t count = 0;
	float peak = 0.0f;
	while (phase < total) {
		const std::uint64_t start = phase;
		buf.fillSine(amplitude, 440.0, kRate, phase);
		auto block = buf.block();
		stage.process(block);
		if (start >= measureFrom) {
			for (float v : buf.planes[0]) {
				sumSq += static_cast<double>(v) * v;
				peak = std::max(peak, std::fabs(v));
			}
			count += blockSize;
		}
	}
	return {linearToDb(static_cast<float>(std::sqrt(sumSq / static_cast<double>(count)))), linearToDb(peak)};
}

void test_compressor_gain_curve()
{
	// Below the knee: untouched.
	CHECK(Compressor::computeGainDb(-30.0f, -20.0f, 4.0f) == 0.0f);
	CHECK(Compressor::computeGainDb(-23.0f, -20.0f, 4.0f) == 0.0f);
	// Above the knee: (1/R - 1) * (x - T).
	CHECK_NEAR(Compressor::computeGainDb(-10.0f, -20.0f, 4.0f), -7.5f, 1e-5f);
	// Knee edges are continuous.
	CHECK_NEAR(Compressor::computeGainDb(-17.0f, -20.0f, 4.0f), -0.75f * 3.0f, 1e-5f);
	// Ratio 1 never reduces.
	CHECK(Compressor::computeGainDb(0.0f, -40.0f, 1.0f) == 0.0f);
	// Monotonic: louder input never gets less reduction.
	float prev = 1.0f;
	for (float x = -60.0f; x <= 0.0f; x += 0.5f) {
		const float g = Compressor::computeGainDb(x, -20.0f, 4.0f);
		CHECK(g <= prev + 1e-6f);
		prev = g;
	}
}

void test_compressor_disabled_is_bit_exact()
{
	Compressor comp;
	comp.prepare(kRate, 2);
	Buffer buf(2, 480);
	std::uint64_t phase = 0;
	buf.fillSine(0.9f, 440.0, kRate, phase);
	const auto original = buf.planes;
	auto block = buf.block();
	comp.process(block);
	CHECK(buf.planes == original);
}

void test_compressor_below_threshold_applies_makeup_only()
{
	Compressor comp;
	comp.prepare(kRate, 2);
	comp.setSettings({-20.0f, 4.0f, 5.0f, 100.0f, 3.0f});
	comp.setEnabled(true);
	const auto out = runSine(comp, -40.0f, 1.0);
	CHECK_NEAR(out.rmsDb, -37.0f, 0.05f);
	CHECK(comp.readGainReductionDb() == 0.0f);
}

void test_compressor_reduces_loud_signal()
{
	Compressor comp;
	comp.prepare(kRate, 2);
	comp.setSettings({-20.0f, 4.0f, 1.0f, 100.0f, 0.0f});
	comp.setEnabled(true);
	// Peak -6 dBFS: static curve says (1/4 - 1) * 14 = -10.5 dB at the peaks.
	const float rms = -6.0f - 3.0103f;
	const auto out = runSine(comp, rms, 2.0);
	CHECK_NEAR(out.peakDb, -16.5f, 1.0f);
	CHECK(out.rmsDb < rms - 8.0f);
	const float gr = comp.readGainReductionDb();
	CHECK(gr < -9.0f && gr > -12.0f);
}

void test_compressor_attack_faster_than_release()
{
	Compressor comp;
	comp.prepare(kRate, 1);
	comp.setSettings({-30.0f, 10.0f, 2.0f, 300.0f, 0.0f});
	comp.setEnabled(true);
	runSine(comp, -6.0f, 0.5, 480, 0.1, 1); // settle into heavy reduction
	comp.readGainReductionDb();

	// Drop well below threshold: reduction must linger (release), not vanish.
	runSine(comp, -60.0f, 0.02, 480, 0.01, 1);
	CHECK(comp.readGainReductionDb() < -10.0f);

	runSine(comp, -60.0f, 2.0, 480, 0.1, 1);
	comp.readGainReductionDb(); // discard the deepest value of the decay
	runSine(comp, -60.0f, 0.05, 480, 0.01, 1);
	CHECK(comp.readGainReductionDb() > -0.5f);
}

void test_leveler_raises_quiet_speech_to_target()
{
	AutoLeveler lev;
	lev.prepare(kRate, 2);
	lev.setSettings({-20.0f, 30.0f, 12.0f, 1.0f, -50.0f});
	lev.setEnabled(true);
	// The target is the OBS meter reading (sample peak); for a sine that is
	// its RMS + 3 dB. Input reads -29, so +9 dB brings it to -20.
	const auto out = runSine(lev, -32.0f, 12.0);
	CHECK_NEAR(out.peakDb, -20.0f, 1.0f);
	CHECK_NEAR(out.rmsDb, -23.0f, 1.0f);
	CHECK_NEAR(lev.currentGainDb(), 9.0f, 1.0f);
}

void test_leveler_respects_max_gain()
{
	AutoLeveler lev;
	lev.prepare(kRate, 2);
	lev.setSettings({-20.0f, 6.0f, 12.0f, 1.0f, -60.0f});
	lev.setEnabled(true);
	const auto out = runSine(lev, -35.0f, 12.0);
	CHECK_NEAR(lev.currentGainDb(), 6.0f, 0.1f);
	CHECK_NEAR(out.rmsDb, -29.0f, 0.5f);
}

void test_leveler_cuts_loud_speech_fast()
{
	AutoLeveler lev;
	lev.prepare(kRate, 2);
	lev.setSettings({-20.0f, 12.0f, 12.0f, 4.0f, -50.0f});
	lev.setEnabled(true);
	// Cuts move kCutSpeedup times faster than boosts: 4 s / 4 = 1 s.
	const auto out = runSine(lev, -10.0f, 5.0); // reads -7 on the meter
	CHECK_NEAR(out.peakDb, -19.0f, 1.0f);       // -12 dB max cut
	CHECK(lev.currentGainDb() < -11.0f);
}

void test_leveler_does_not_boost_silence_or_noise()
{
	// Fresh leveler fed only low room noise: gain must stay at 0 dB.
	AutoLeveler lev;
	lev.prepare(kRate, 2);
	lev.setSettings({-20.0f, 24.0f, 12.0f, 0.5f, -50.0f});
	lev.setEnabled(true);
	const auto noise = runSine(lev, -65.0f, 10.0);
	CHECK(lev.currentGainDb() == 0.0f);
	CHECK_NEAR(noise.rmsDb, -65.0f, 0.05f);

	// After speech, a pause holds the gain instead of creeping up further.
	runSine(lev, -30.0f, 8.0);
	const float afterSpeech = lev.currentGainDb();
	runSine(lev, -70.0f, 10.0);
	CHECK_NEAR(lev.currentGainDb(), afterSpeech, 0.2f); // ~0.06 dB in the first 60 ms, then frozen
}

void test_leveler_block_size_independent()
{
	auto settle = [](std::uint32_t blockSize) {
		AutoLeveler lev;
		lev.prepare(kRate, 1);
		lev.setSettings({-20.0f, 12.0f, 12.0f, 1.0f, -50.0f});
		lev.setEnabled(true);
		runSine(lev, -26.0f, 1.5, blockSize, 0.1, 1);
		return lev.currentGainDb();
	};
	CHECK_NEAR(settle(128), settle(1024), 0.3f);
}

void test_leveler_disabled_is_bit_exact_and_reenable_resets()
{
	AutoLeveler lev;
	lev.prepare(kRate, 2);
	lev.setSettings({-20.0f, 12.0f, 12.0f, 0.5f, -50.0f});
	lev.setEnabled(true);
	runSine(lev, -35.0f, 5.0);
	CHECK(lev.currentGainDb() > 10.0f);

	lev.setEnabled(false);
	Buffer buf(2, 480);
	std::uint64_t phase = 0;
	buf.fillSine(0.2f, 440.0, kRate, phase);
	const auto original = buf.planes;
	auto block = buf.block();
	lev.process(block);
	CHECK(buf.planes == original);

	// Switching back on starts from 0 dB instead of the stale +12 dB.
	lev.setEnabled(true);
	runSine(lev, -70.0f, 0.1);
	CHECK(lev.currentGainDb() == 0.0f);
}

void test_voice_chain_order_and_bypass()
{
	VoiceChain chain;
	chain.engine.prepare(kRate, 2);
	CHECK(chain.engine.stageCount() == 3);

	// Leveler / compressor off: below the ceiling the chain is a bit-exact
	// pass-through, delayed by the limiter's look-ahead.
	const std::uint32_t lat = chain.limiter.latencyFrames();
	Buffer buf(2, 480);
	std::uint64_t phase = 0;
	buf.fillSine(0.5f, 440.0, kRate, phase);
	const auto original = buf.planes;
	auto block = buf.block();
	chain.engine.process(block);
	bool delayedExact = true;
	for (std::uint32_t i = lat; i < 480; ++i)
		delayedExact = delayedExact && buf.planes[0][i] == original[0][i - lat];
	CHECK(delayedExact);

	// Leveler runs first: the compressor sees the boosted signal. A quiet
	// voice that alone stays under the threshold gets compressed once the
	// leveler has lifted it above.
	chain.leveler.setSettings({-12.0f, 24.0f, 12.0f, 0.5f, -50.0f});
	chain.leveler.setEnabled(true);
	chain.compressor.setSettings({-20.0f, 4.0f, 5.0f, 100.0f, 0.0f});
	chain.compressor.setEnabled(true);
	const float quiet = -30.0f; // peak -27 dBFS: below the -20 dB threshold
	CHECK(Compressor::computeGainDb(quiet + 3.0f, -20.0f, 4.0f) == 0.0f);
	runSine(chain.engine, quiet, 8.0);
	CHECK(chain.compressor.readGainReductionDb() < -3.0f);
}

void test_strength_mapping()
{
	const auto mid = compressorFromStrength(30.0f);
	CHECK_NEAR(mid.threshold, params::kCompThreshold.defaultValue, 1e-4f);
	CHECK_NEAR(mid.ratio, params::kCompRatio.defaultValue, 1e-4f);
	const auto gentle = compressorFromStrength(0.0f);
	CHECK_NEAR(gentle.threshold, -9.0f, 1e-4f); // only louder-than-normal speech
	CHECK_NEAR(gentle.ratio, 1.5f, 1e-4f);
	const auto strong = compressorFromStrength(250.0f); // clamped to 100 %
	CHECK_NEAR(strong.threshold, -21.0f, 1e-4f);
	CHECK_NEAR(strong.ratio, 4.0f, 1e-4f);

	// Auto makeup keeps normal speech (-9 dBFS) where it was.
	for (float sPct : {0.0f, 30.0f, 60.0f, 100.0f}) {
		const auto tr = compressorFromStrength(sPct);
		const float makeup = Compressor::autoMakeupDb(tr.threshold, tr.ratio, levels::kNormalPeakDb);
		CHECK_NEAR(Compressor::computeGainDb(levels::kNormalPeakDb, tr.threshold, tr.ratio) + makeup, 0.0f,
			   1e-5f);
		CHECK(makeup >= 0.0f && makeup <= 10.0f);
	}
}

void test_waveform_buckets()
{
	WaveformCapture wave;
	wave.prepare(kRate); // 10 ms = 480 frames
	CHECK(wave.bucketFrames() == 480);

	// 700 frames: one full bucket now, 220 carried into the next block.
	std::vector<float> a(700, 0.0f);
	a[10] = 0.5f;
	a[20] = -0.25f;
	a[600] = 0.9f; // lands in the second bucket
	float *pa[1] = {a.data()};
	wave.process({pa, 1, 700});

	WaveBucket out[4];
	CHECK(wave.read(out, 4) == 1);
	CHECK(out[0].max == 0.5f && out[0].min == -0.25f);

	std::vector<float> b(260, 0.0f); // completes bucket 2 (220 + 260 = 480)
	b[5] = -0.7f;
	float *pb[1] = {b.data()};
	wave.process({pb, 1, 260});
	CHECK(wave.read(out, 4) == 1);
	CHECK(out[0].max == 0.9f && out[0].min == -0.7f);
	CHECK(wave.read(out, 4) == 0);
}

void test_waveform_across_channels_and_null_planes()
{
	WaveformCapture wave;
	wave.prepare(kRate);
	std::vector<float> l(480, 0.1f), r(480, -0.3f);
	float *planes[3] = {l.data(), nullptr, r.data()};
	wave.process({planes, 3, 480});
	WaveBucket out;
	CHECK(wave.read(&out, 1) == 1);
	CHECK(out.min == -0.3f && out.max == 0.1f);
}

void test_engine_waveform_toggle_and_alignment()
{
	GainStage half(0.5f);
	AudioEngine engine;
	engine.addStage(&half);
	engine.prepare(kRate, 1);

	Buffer buf(1, 240); // half a bucket per block
	for (auto &v : buf.planes[0])
		v = 0.8f;

	// Off by default: nothing captured.
	auto block = buf.block();
	engine.process(block);
	WaveBucket in[8], out[8];
	CHECK(engine.inputWaveform().read(in, 8) == 0);

	// On: input and output buckets arrive together and reflect the stage.
	engine.setWaveformEnabled(true);
	for (int i = 0; i < 6; ++i) {
		for (auto &v : buf.planes[0])
			v = 0.8f;
		auto b = buf.block();
		engine.process(b);
	}
	const auto nIn = engine.inputWaveform().read(in, 8);
	const auto nOut = engine.outputWaveform().read(out, 8);
	CHECK(nIn == 3 && nOut == 3);
	CHECK_NEAR(in[0].max, 0.8f, 1e-6f);
	CHECK_NEAR(out[0].max, 0.4f, 1e-6f);

	// Turning it back on drops the stale partial bucket: still aligned.
	engine.setWaveformEnabled(false);
	auto c = buf.block();
	engine.process(c); // half a bucket would be pending if capture ran
	engine.setWaveformEnabled(true);
	for (int i = 0; i < 2; ++i) {
		auto d = buf.block();
		engine.process(d);
	}
	CHECK(engine.inputWaveform().read(in, 8) == 1);
	CHECK(engine.outputWaveform().read(out, 8) == 1);
}

void test_waveform_full_ring_drops_without_blocking()
{
	WaveformCapture wave(4);
	wave.prepare(kRate);
	std::vector<float> a(480 * 10, 0.2f);
	float *p[1] = {a.data()};
	wave.process({p, 1, static_cast<std::uint32_t>(a.size())});
	WaveBucket out[16];
	CHECK(wave.read(out, 16) == 4);
}

// ---------------------------------------------------------------------------
// Auto setup: measurement and proposal
// ---------------------------------------------------------------------------

// Builds 50 ms frames: speech segments at the given RMS levels with short
// pauses of room noise between them, like reading the three sample lines.
std::vector<FrameStats> voiceSample(float noiseDb, std::initializer_list<float> speechDbs, int framesPerSegment = 50,
				    int pauseFrames = 10)
{
	std::vector<FrameStats> frames;
	auto push = [&](float db, int n) {
		const float ms = std::pow(10.0f, db / 10.0f);
		const float peak = std::sqrt(ms) * 1.8f; // speech crest factor ~5 dB
		for (int i = 0; i < n; ++i) {
			// +-3 dB syllable movement inside a segment
			const float wobble = std::pow(10.0f, (static_cast<float>(i % 7) - 3.0f) / 10.0f);
			frames.push_back({ms * wobble, peak});
		}
	};
	push(noiseDb, pauseFrames);
	for (float db : speechDbs) {
		push(db, framesPerSegment);
		push(noiseDb, pauseFrames);
	}
	return frames;
}

void test_analysis_capture_frames()
{
	AnalysisCapture cap;
	cap.prepare(kRate); // 50 ms = 2400 frames
	Buffer buf(2, 1200);
	std::uint64_t phase = 0;
	for (int i = 0; i < 5; ++i) { // 6000 frames: 2 full analysis frames
		buf.fillSine(0.5f, 440.0, kRate, phase);
		cap.process(buf.block());
	}
	FrameStats out[4];
	CHECK(cap.read(out, 4) == 2);
	CHECK_NEAR(linearToDb(std::sqrt(out[0].meanSquare)), linearToDb(0.5f / std::numbers::sqrt2_v<float>), 0.1f);
	CHECK_NEAR(out[0].peak, 0.5f, 1e-3f);
}

void test_calibration_typical_voice()
{
	// Normal / quieter / louder lines in a quiet room.
	const auto frames = voiceSample(-62.0f, {-24.0f, -30.0f, -18.0f});
	const auto r = analyzeVoice(frames, -20.0f, 30.0f);
	CHECK(r.usable);
	CHECK(r.issues == kIssueNone);
	CHECK_NEAR(r.stats.noiseDb, -62.0f, 3.5f);
	CHECK_NEAR(r.stats.speechDb, -24.0f, 3.5f);
	CHECK(r.stats.speechSeconds > 6.0f);
	// Gate above noise, below quiet speech.
	CHECK(r.proposal.gateDb > r.stats.noiseDb);
	CHECK(r.proposal.gateDb < r.stats.quietSpeechDb - 5.0f);
	// Boost reaches the quieter line; cut reaches the louder line.
	CHECK(r.proposal.maxGainDb >= -20.0f - r.stats.quietSpeechPeakDb - 0.5f);
	CHECK(r.proposal.maxCutDb >= r.stats.loudSpeechPeakDb - (-20.0f));
	CHECK_NEAR(r.stats.speechPeakDb, r.stats.speechDb + 5.1f, 1.5f); // sample crest factor
	CHECK(r.proposal.compStrength >= 10.0f && r.proposal.compStrength <= 85.0f);
}

void test_calibration_quiet_speaker_gets_more_boost()
{
	const auto normal = analyzeVoice(voiceSample(-65.0f, {-24.0f, -28.0f, -20.0f}), -20.0f, 30.0f);
	const auto quiet = analyzeVoice(voiceSample(-65.0f, {-36.0f, -40.0f, -32.0f}), -20.0f, 30.0f);
	CHECK(quiet.usable && normal.usable);
	CHECK(quiet.proposal.maxGainDb > normal.proposal.maxGainDb + 8.0f);
	CHECK(!(quiet.issues & kIssueTooQuiet)); // -36 dBFS is quiet but workable
}

void test_calibration_wide_dynamics_get_more_compression()
{
	const auto narrow = analyzeVoice(voiceSample(-65.0f, {-24.0f, -26.0f, -22.0f}), -20.0f, 30.0f);
	const auto wide = analyzeVoice(voiceSample(-65.0f, {-24.0f, -38.0f, -10.0f}), -20.0f, 30.0f);
	CHECK(wide.proposal.compStrength > narrow.proposal.compStrength + 20.0f);
	CHECK(wide.proposal.maxCutDb > narrow.proposal.maxCutDb);
}

void test_calibration_noisy_room()
{
	const auto r = analyzeVoice(voiceSample(-40.0f, {-20.0f, -24.0f, -16.0f}), -20.0f, 30.0f);
	CHECK(r.usable);
	CHECK(r.issues & kIssueNoisy);
	// Gate moves up with the noise so the noise itself is not boosted.
	CHECK(r.proposal.gateDb > -40.0f);
	CHECK(r.proposal.gateDb <= r.stats.quietSpeechDb - 5.5f);
}

void test_calibration_problems_detected()
{
	// Hardly any speech: retry.
	const auto silent = analyzeVoice(voiceSample(-65.0f, {-25.0f}, 20, 150), -20.0f, 30.0f);
	CHECK(!silent.usable);
	CHECK(silent.issues & kIssueNotEnoughSpeech);

	// Very low mic level.
	const auto low = analyzeVoice(voiceSample(-85.0f, {-54.0f, -58.0f, -50.0f}), -20.0f, 30.0f);
	CHECK(low.issues & kIssueTooQuiet);

	// Clipping.
	auto hot = voiceSample(-65.0f, {-10.0f, -14.0f, -6.0f});
	hot[80].peak = 1.0f;
	const auto clip = analyzeVoice(hot, -20.0f, 30.0f);
	CHECK(clip.issues & kIssueClipping);

	// Proposals always stay inside the parameter ranges.
	for (const auto *r : {&low, &clip}) {
		CHECK(r->proposal.gateDb >= params::kLevelerGate.min && r->proposal.gateDb <= params::kLevelerGate.max);
		CHECK(r->proposal.maxGainDb <= params::kLevelerMaxGain.max);
		CHECK(r->proposal.maxCutDb <= params::kLevelerMaxCut.max);
	}
}

void test_engine_analysis_toggle()
{
	AudioEngine engine;
	engine.prepare(kRate, 1);
	Buffer buf(1, 2400);
	auto b = buf.block();
	engine.process(b);
	FrameStats out[4];
	CHECK(engine.analysis().read(out, 4) == 0); // off by default
	engine.setAnalysisEnabled(true);
	auto c = buf.block();
	engine.process(c);
	CHECK(engine.analysis().read(out, 4) == 1);
}

// ---------------------------------------------------------------------------
// Peak limiter (shared ceiling)
// ---------------------------------------------------------------------------

void test_limiter_transparent_below_ceiling()
{
	PeakLimiter lim;
	lim.prepare(kRate, 1);
	CHECK(lim.latencyFrames() == 72); // 1.5 ms at 48 kHz
	Buffer buf(1, 1000);
	std::uint64_t phase = 0;
	buf.fillSine(0.7f, 440.0, kRate, phase); // -3.1 dBFS peak, under -1
	const auto original = buf.planes;
	auto b = buf.block();
	lim.process(b);
	bool exact = true;
	for (std::uint32_t i = 72; i < 1000; ++i)
		exact = exact && buf.planes[0][i] == original[0][i - 72];
	CHECK(exact);
	CHECK(lim.readGainReductionDb() == 0.0f);
}

void test_limiter_never_exceeds_ceiling()
{
	PeakLimiter lim;
	lim.prepare(kRate, 2);
	const float ceiling = dbToLinear(levels::kCeilingDb);
	std::uint32_t seed = 12345;
	auto rnd = [&seed] {
		seed = seed * 1664525u + 1013904223u;
		return static_cast<float>(seed >> 8) / static_cast<float>(1u << 24); // 0..1
	};
	float maxOut = 0.0f;
	const std::uint32_t sizes[] = {1, 7, 64, 333, 480, 1024};
	for (int round = 0; round < 300; ++round) {
		const std::uint32_t n = sizes[round % 6];
		Buffer buf(2, n);
		// Speech-like noise with occasional shouts up to +12 dBFS
		const float level = (round % 17 == 0) ? 4.0f : 0.3f + rnd();
		for (std::uint32_t i = 0; i < n; ++i) {
			buf.planes[0][i] = (rnd() * 2.0f - 1.0f) * level;
			buf.planes[1][i] = (rnd() * 2.0f - 1.0f) * level * 0.5f;
		}
		auto b = buf.block();
		lim.process(b);
		for (const auto &plane : buf.planes)
			for (float v : plane)
				maxOut = std::max(maxOut, std::fabs(v));
	}
	CHECK(maxOut <= ceiling * 1.000001f);
	CHECK(maxOut > ceiling * 0.9f); // it was actually limiting
	CHECK(lim.readGainReductionDb() < -6.0f);
}

void test_limiter_recovers_after_burst()
{
	PeakLimiter lim;
	lim.prepare(kRate, 1);
	runSine(lim, 6.0f, 0.2, 480, 0.1, 1); // +9 dBFS peak: heavy limiting
	lim.readGainReductionDb();
	runSine(lim, -20.0f, 1.0, 480, 0.1, 1);
	lim.readGainReductionDb();
	const auto out = runSine(lim, -20.0f, 0.1, 480, 0.05, 1);
	CHECK(lim.readGainReductionDb() > -0.01f); // back to unity (asymptotically)
	CHECK_NEAR(out.rmsDb, -20.0f, 0.05f);
}

void test_chain_meets_obs_targets()
{
	// Default settings: a steady normal voice ends up reading about -9 dBFS on
	// the OBS meter, and a shout 15 dB louder never exceeds -1 dBFS.
	VoiceChain chain;
	chain.engine.prepare(kRate, 2);
	chain.leveler.setSettings({levels::kLevelerTargetDb, 12.0f, 12.0f, 1.0f, -50.0f});
	chain.leveler.setEnabled(true);
	const auto tr = compressorFromStrength(30.0f);
	chain.compressor.setSettings({tr.threshold, tr.ratio, 10.0f, 150.0f,
				      Compressor::autoMakeupDb(tr.threshold, tr.ratio, levels::kNormalPeakDb)});
	chain.compressor.setEnabled(true);

	const auto normal = runSine(chain.engine, -24.0f, 10.0);
	CHECK_NEAR(normal.peakDb, levels::kLevelerTargetDb, 1.0f); // a sine has no dips between syllables
	CHECK(normal.peakDb < levels::kCeilingDb - 3.0f);

	const auto shout = runSine(chain.engine, -9.0f, 0.3, 480, 0.3); // +15 dB, before the leveler reacts
	CHECK(shout.peakDb <= levels::kCeilingDb + 1e-4f);
}

} // namespace

int main()
{
	const std::pair<const char *, std::function<void()>> tests[] = {
		{"db_conversion", test_db_conversion},
		{"passthrough_is_bit_exact", test_passthrough_is_bit_exact},
		{"meter_sine_levels", test_meter_sine_levels},
		{"meter_variable_block_sizes", test_meter_variable_block_sizes},
		{"meter_handles_null_planes_and_empty_blocks", test_meter_handles_null_planes_and_empty_blocks},
		{"stage_chain_order_and_meters", test_stage_chain_order_and_meters},
		{"stage_capacity", test_stage_capacity},
		{"parameters", test_parameters},
		{"ring_buffer_basic", test_ring_buffer_basic},
		{"ring_buffer_threads", test_ring_buffer_threads},
		{"compressor_gain_curve", test_compressor_gain_curve},
		{"compressor_disabled_is_bit_exact", test_compressor_disabled_is_bit_exact},
		{"compressor_below_threshold_applies_makeup_only", test_compressor_below_threshold_applies_makeup_only},
		{"compressor_reduces_loud_signal", test_compressor_reduces_loud_signal},
		{"compressor_attack_faster_than_release", test_compressor_attack_faster_than_release},
		{"leveler_raises_quiet_speech_to_target", test_leveler_raises_quiet_speech_to_target},
		{"leveler_respects_max_gain", test_leveler_respects_max_gain},
		{"leveler_cuts_loud_speech_fast", test_leveler_cuts_loud_speech_fast},
		{"leveler_does_not_boost_silence_or_noise", test_leveler_does_not_boost_silence_or_noise},
		{"leveler_block_size_independent", test_leveler_block_size_independent},
		{"leveler_disabled_is_bit_exact_and_reenable_resets",
		 test_leveler_disabled_is_bit_exact_and_reenable_resets},
		{"voice_chain_order_and_bypass", test_voice_chain_order_and_bypass},
		{"strength_mapping", test_strength_mapping},
		{"waveform_buckets", test_waveform_buckets},
		{"waveform_across_channels_and_null_planes", test_waveform_across_channels_and_null_planes},
		{"engine_waveform_toggle_and_alignment", test_engine_waveform_toggle_and_alignment},
		{"waveform_full_ring_drops_without_blocking", test_waveform_full_ring_drops_without_blocking},
		{"analysis_capture_frames", test_analysis_capture_frames},
		{"calibration_typical_voice", test_calibration_typical_voice},
		{"calibration_quiet_speaker_gets_more_boost", test_calibration_quiet_speaker_gets_more_boost},
		{"calibration_wide_dynamics_get_more_compression", test_calibration_wide_dynamics_get_more_compression},
		{"calibration_noisy_room", test_calibration_noisy_room},
		{"calibration_problems_detected", test_calibration_problems_detected},
		{"engine_analysis_toggle", test_engine_analysis_toggle},
		{"limiter_transparent_below_ceiling", test_limiter_transparent_below_ceiling},
		{"limiter_never_exceeds_ceiling", test_limiter_never_exceeds_ceiling},
		{"limiter_recovers_after_burst", test_limiter_recovers_after_burst},
		{"chain_meets_obs_targets", test_chain_meets_obs_targets},
	};

	for (const auto &[name, fn] : tests) {
		const int before = g_failures;
		fn();
		std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", name);
	}
	std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
