#include "level_meter_widget.hpp"

#include <nvoice/engine/levels.hpp>

#include <QPainter>

#include <algorithm>

namespace nvoice::ui {

namespace {
namespace lv = engine::levels;
constexpr qint64 kPeakHoldMs = 1500;
constexpr float kPeakFallDbPerSec = 20.0f;
} // namespace

LevelMeterWidget::LevelMeterWidget(QWidget *parent) : QWidget(parent)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	peakTimer_.start();
	frameTimer_.start();
}

void LevelMeterWidget::setLevels(float, float peakDb)
{
	const float elapsedSec = static_cast<float>(frameTimer_.restart()) / 1000.0f;

	// OBS meter ballistics: immediate attack, linear decay in dB.
	if (peakDb >= displayDb_)
		displayDb_ = peakDb;
	else
		displayDb_ = std::max(peakDb, displayDb_ - static_cast<float>(lv::kObsMeterDecayDbPerSec) * elapsedSec);

	if (peakDb >= heldPeakDb_) {
		heldPeakDb_ = peakDb;
		peakTimer_.restart();
	} else if (peakTimer_.elapsed() > kPeakHoldMs) {
		heldPeakDb_ = std::max(peakDb, heldPeakDb_ - kPeakFallDbPerSec * elapsedSec);
	}
	update();
}

void LevelMeterWidget::clear()
{
	displayDb_ = -120.0f;
	heldPeakDb_ = -120.0f;
	update();
}

void LevelMeterWidget::setColors(const QColor &track, const QColor &green, const QColor &yellow, const QColor &red)
{
	track_ = track;
	green_ = green;
	yellow_ = yellow;
	red_ = red;
	update();
}

QSize LevelMeterWidget::sizeHint() const
{
	return {300, 14};
}

QSize LevelMeterWidget::minimumSizeHint() const
{
	return {80, 14};
}

float LevelMeterWidget::toFraction(float db) const
{
	return std::clamp((db - kFloorDb) / (0.0f - kFloorDb), 0.0f, 1.0f);
}

void LevelMeterWidget::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);

	const QRectF bar = QRectF(rect()).adjusted(0, 2, 0, -2);
	const qreal radius = 3.0;
	auto x = [&](float db) {
		return bar.left() + bar.width() * toFraction(db);
	};

	// Track, tinted by zone like OBS's meter background.
	p.setBrush(track_);
	p.drawRoundedRect(bar, radius, radius);
	auto tint = [&](const QColor &c, float from, float to, qreal alpha) {
		QColor t = c;
		t.setAlphaF(alpha);
		p.setBrush(t);
		p.drawRect(QRectF(x(from), bar.top(), x(to) - x(from), bar.height()));
	};
	tint(yellow_, lv::kObsYellowDb, lv::kObsRedDb, 0.18);
	tint(red_, lv::kObsRedDb, 0.0f, 0.18);

	// Level, coloured by zone.
	auto fill = [&](const QColor &c, float from, float to) {
		const float top = std::min(displayDb_, to);
		if (top <= from)
			return;
		p.setBrush(c);
		p.drawRect(QRectF(x(from), bar.top(), x(top) - x(from), bar.height()));
	};
	fill(green_, kFloorDb, lv::kObsYellowDb);
	fill(yellow_, lv::kObsYellowDb, lv::kObsRedDb);
	fill(red_, lv::kObsRedDb, 0.0f);

	// Held peak
	if (heldPeakDb_ > kFloorDb) {
		const QColor c = heldPeakDb_ >= lv::kObsRedDb ? red_
							      : (heldPeakDb_ >= lv::kObsYellowDb ? yellow_ : green_);
		p.setBrush(c.lighter(115));
		p.drawRect(QRectF(std::min(x(heldPeakDb_), bar.right() - 2.0), bar.top() - 1, 2.0, bar.height() + 2));
	}

	// Zone ticks at -20 and -9 dBFS
	QColor tick = palette().color(QPalette::WindowText);
	tick.setAlphaF(0.55f);
	p.setBrush(tick);
	for (float db : {lv::kObsYellowDb, lv::kObsRedDb})
		p.drawRect(QRectF(x(db) - 0.5, bar.top() - 2, 1.0, bar.height() + 4));
}

} // namespace nvoice::ui
