// Unit tests for the OBS-independent audio engine.
// Dependency-free on purpose: run with `ctest` or execute the binary directly.

#include "nvoice/engine/audio_engine.hpp"
#include "nvoice/engine/auto_leveler.hpp"
#include "nvoice/engine/compressor.hpp"
#include "nvoice/engine/db.hpp"
#include "nvoice/engine/parameters.hpp"
#include "nvoice/engine/spsc_ring_buffer.hpp"
#include "nvoice/engine/voice_chain.hpp"

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
	const auto out = runSine(lev, -32.0f, 12.0);
	CHECK_NEAR(out.rmsDb, -20.0f, 1.0f);
	CHECK_NEAR(lev.currentGainDb(), 12.0f, 1.0f);
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
	const auto out = runSine(lev, -10.0f, 5.0);
	CHECK_NEAR(out.rmsDb, -20.0f, 1.0f);
	CHECK(lev.currentGainDb() < -9.0f);
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
	CHECK(chain.engine.stageCount() == 2);

	// Everything off: the chain is a bit-exact pass-through.
	Buffer buf(2, 480);
	std::uint64_t phase = 0;
	buf.fillSine(0.5f, 440.0, kRate, phase);
	const auto original = buf.planes;
	auto block = buf.block();
	chain.engine.process(block);
	CHECK(buf.planes == original);

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
	CHECK_NEAR(gentle.threshold, -10.0f, 1e-4f);
	CHECK_NEAR(gentle.ratio, 1.5f, 1e-4f);
	const auto strong = compressorFromStrength(250.0f); // clamped to 100 %
	CHECK_NEAR(strong.threshold, -40.0f, 1e-4f);
	CHECK_NEAR(strong.ratio, 6.5f, 1e-4f);
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
	};

	for (const auto &[name, fn] : tests) {
		const int before = g_failures;
		fn();
		std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", name);
	}
	std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
