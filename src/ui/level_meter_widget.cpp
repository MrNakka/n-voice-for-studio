#include "level_meter_widget.hpp"

#include <QLinearGradient>
#include <QPainter>

#include <algorithm>

namespace nvoice::ui {

namespace {
constexpr qint64 kPeakHoldMs = 1500;
constexpr float kPeakFallDbPerSec = 20.0f;
} // namespace

LevelMeterWidget::LevelMeterWidget(QWidget *parent) : QWidget(parent)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	peakTimer_.start();
	frameTimer_.start();
}

void LevelMeterWidget::setLevels(float rmsDb, float peakDb)
{
	const float elapsedSec = static_cast<float>(frameTimer_.restart()) / 1000.0f;
	rmsDb_ = rmsDb;

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
	rmsDb_ = -120.0f;
	heldPeakDb_ = -120.0f;
	update();
}

void LevelMeterWidget::setColors(const QColor &track, const QColor &fillStart, const QColor &fillEnd, const QColor &hot)
{
	track_ = track;
	fillStart_ = fillStart;
	fillEnd_ = fillEnd;
	hot_ = hot;
	update();
}

QSize LevelMeterWidget::sizeHint() const
{
	return {300, 12};
}

QSize LevelMeterWidget::minimumSizeHint() const
{
	return {80, 12};
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

	const QRectF bar = QRectF(rect()).adjusted(0, 1, 0, -1);
	const qreal radius = bar.height() / 2.0;

	p.setBrush(track_);
	p.drawRoundedRect(bar, radius, radius);

	const qreal fillWidth = bar.width() * toFraction(rmsDb_);
	if (fillWidth > 1.0) {
		QLinearGradient gradient(bar.topLeft(), bar.topRight());
		gradient.setColorAt(0.0, fillStart_);
		gradient.setColorAt(toFraction(kHotDb) - 0.001, fillEnd_);
		gradient.setColorAt(toFraction(kHotDb), hot_);
		gradient.setColorAt(1.0, hot_);
		p.setBrush(gradient);
		p.drawRoundedRect(QRectF(bar.left(), bar.top(), std::max(fillWidth, bar.height()), bar.height()),
				  radius, radius);
	}

	if (heldPeakDb_ > kFloorDb) {
		const qreal x = bar.left() + bar.width() * toFraction(heldPeakDb_);
		p.setBrush(heldPeakDb_ >= kHotDb ? hot_ : fillEnd_.lighter(130));
		p.drawRect(QRectF(std::min(x, bar.right() - 2.0), bar.top(), 2.0, bar.height()));
	}
}

} // namespace nvoice::ui
