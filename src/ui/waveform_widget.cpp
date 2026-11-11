#include "waveform_widget.hpp"

#include <nvoice/engine/db.hpp>

#include <obs-module.h>

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace nvoice::ui {

namespace {

// The display is logarithmic so quiet speech is still visible: -60 dBFS at
// the centre line, 0 dBFS at the edge (same range as the level meters).
constexpr float kFloorDb = -60.0f;

float toFraction(float amplitude)
{
	const float a = std::fabs(amplitude);
	if (a <= 0.0f)
		return 0.0f;
	return std::clamp((20.0f * std::log10(a) - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
}

QPainterPath envelope(const WaveHistory &h, bool output, const QRectF &area)
{
	const int n = h.capacity();
	const qreal cy = area.center().y();
	const qreal half = area.height() / 2.0;
	const qreal step = area.width() / std::max(1, n - 1);

	QPainterPath path;
	for (int i = 0; i < n; ++i) {
		const auto &b = output ? h.output(i) : h.input(i);
		const QPointF p(area.left() + i * step, cy - toFraction(std::max(b.max, 0.0f)) * half);
		i == 0 ? path.moveTo(p) : path.lineTo(p);
	}
	for (int i = n - 1; i >= 0; --i) {
		const auto &b = output ? h.output(i) : h.input(i);
		path.lineTo(area.left() + i * step, cy + toFraction(std::min(b.min, 0.0f)) * half);
	}
	path.closeSubpath();
	return path;
}

} // namespace

// ---------------------------------------------------------------------------
// WaveHistory
// ---------------------------------------------------------------------------

WaveHistory::WaveHistory(int columns)
	: input_(static_cast<std::size_t>(std::max(2, columns)), engine::WaveBucket{0.0f, 0.0f}),
	  output_(input_.size(), engine::WaveBucket{0.0f, 0.0f})
{
}

void WaveHistory::clear()
{
	std::fill(input_.begin(), input_.end(), engine::WaveBucket{0.0f, 0.0f});
	std::fill(output_.begin(), output_.end(), engine::WaveBucket{0.0f, 0.0f});
	head_ = 0;
}

void WaveHistory::append(const engine::WaveBucket *input, const engine::WaveBucket *output, std::size_t count)
{
	for (std::size_t i = 0; i < count; ++i) {
		// Overwrite the oldest slot; it becomes the newest.
		input_[head_] = input[i];
		output_[head_] = output[i];
		head_ = (head_ + 1) % input_.size();
	}
}

// ---------------------------------------------------------------------------
// WaveformWidget
// ---------------------------------------------------------------------------

WaveformWidget::WaveformWidget(const WaveHistory *history, QWidget *parent) : QWidget(parent), history_(history)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setFixedHeight(96);
}

void WaveformWidget::setColors(const QColor &grid, const QColor &input, const QColor &output, const QColor &threshold,
			       const QColor &text)
{
	grid_ = grid;
	input_ = input;
	output_ = output;
	threshold_ = threshold;
	text_ = text;
	update();
}

void WaveformWidget::setThresholdDb(std::optional<float> db)
{
	if (db == thresholdDb_)
		return;
	thresholdDb_ = db;
	update();
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	const QRectF area = QRectF(rect()).adjusted(1, 16, -1, -2);
	const qreal cy = area.center().y();
	const qreal half = area.height() / 2.0;

	// Centre line and -20 dBFS guides
	p.setPen(QPen(grid_, 1.0));
	p.drawLine(QPointF(area.left(), cy), QPointF(area.right(), cy));
	QPen guide(grid_, 1.0, Qt::DotLine);
	p.setPen(guide);
	const qreal g = toFraction(engine::dbToLinear(-20.0f)) * half;
	p.drawLine(QPointF(area.left(), cy - g), QPointF(area.right(), cy - g));
	p.drawLine(QPointF(area.left(), cy + g), QPointF(area.right(), cy + g));

	if (history_) {
		QColor in = input_;
		in.setAlphaF(0.45f);
		p.setPen(Qt::NoPen);
		p.setBrush(in);
		p.drawPath(envelope(*history_, false, area));

		QColor out = output_;
		out.setAlphaF(0.85f);
		p.setBrush(out);
		p.drawPath(envelope(*history_, true, area));
	}

	if (thresholdDb_) {
		const qreal t = toFraction(engine::dbToLinear(*thresholdDb_)) * half;
		QPen dash(threshold_, 1.5, Qt::DashLine);
		p.setPen(dash);
		p.drawLine(QPointF(area.left(), cy - t), QPointF(area.right(), cy - t));
		p.drawLine(QPointF(area.left(), cy + t), QPointF(area.right(), cy + t));
	}

	// Legend
	QFont f = font();
	f.setPixelSize(10);
	p.setFont(f);
	qreal x = 2;
	auto legend = [&](const QColor &c, const char *key, bool dashed) {
		if (dashed) {
			p.setPen(QPen(c, 1.5, Qt::DashLine));
			p.drawLine(QPointF(x, 7), QPointF(x + 12, 7));
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(c);
			p.drawRoundedRect(QRectF(x, 3, 10, 8), 2, 2);
		}
		x += 15;
		const QString label = QString::fromUtf8(obs_module_text(key));
		p.setPen(text_);
		p.drawText(QPointF(x, 11), label);
		x += p.fontMetrics().horizontalAdvance(label) + 12;
	};
	QColor in = input_;
	in.setAlphaF(0.6f);
	legend(in, "NVoice.Wave.Input", false);
	legend(output_, "NVoice.Wave.Output", false);
	if (thresholdDb_)
		legend(threshold_, "NVoice.Wave.Threshold", true);
}

} // namespace nvoice::ui
