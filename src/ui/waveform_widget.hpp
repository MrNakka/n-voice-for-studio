// N Voice for Studio - UI
// Scrolling waveform of the input and the processed output (requirements
// §4.7). The newest audio is on the right.
#pragma once

#include <nvoice/engine/waveform_capture.hpp>

#include <QColor>
#include <QWidget>

#include <optional>
#include <vector>

namespace nvoice::ui {

// Fixed-length history shared by every waveform view of one filter.
class WaveHistory {
public:
	explicit WaveHistory(int columns = 400); // 4 s of 10 ms buckets

	void clear();
	void append(const engine::WaveBucket *input, const engine::WaveBucket *output, std::size_t count);

	int capacity() const { return static_cast<int>(input_.size()); }
	// i = 0 is the oldest of the last capacity() buckets (silence if not yet filled).
	const engine::WaveBucket &input(int i) const { return input_[index(i)]; }
	const engine::WaveBucket &output(int i) const { return output_[index(i)]; }

private:
	std::size_t index(int i) const { return (head_ + static_cast<std::size_t>(i)) % input_.size(); }

	std::vector<engine::WaveBucket> input_;
	std::vector<engine::WaveBucket> output_;
	std::size_t head_ = 0; // slot of the oldest bucket
};

class WaveformWidget : public QWidget {
public:
	explicit WaveformWidget(const WaveHistory *history, QWidget *parent = nullptr);

	void setColors(const QColor &grid, const QColor &input, const QColor &output, const QColor &threshold,
		       const QColor &text);
	// Threshold in dBFS of the *input*, drawn as dashed lines; nullopt hides it.
	void setThresholdDb(std::optional<float> db);

	QSize sizeHint() const override { return {300, 96}; }
	QSize minimumSizeHint() const override { return {120, 72}; }

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	const WaveHistory *history_;
	std::optional<float> thresholdDb_;
	QColor grid_{"#2E3547"};
	QColor input_{"#98A1B5"};
	QColor output_{"#4C8DFF"};
	QColor threshold_{"#FF6B5E"};
	QColor text_{"#98A1B5"};
};

} // namespace nvoice::ui
