#include "toggle_switch.hpp"

#include <QPainter>

namespace nvoice::ui {

ToggleSwitch::ToggleSwitch(QWidget *parent) : QAbstractButton(parent)
{
	setCheckable(true);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::StrongFocus);
}

void ToggleSwitch::setColors(const QColor &on, const QColor &off, const QColor &knob)
{
	on_ = on;
	off_ = off;
	knob_ = knob;
	update();
}

QSize ToggleSwitch::sizeHint() const
{
	return {64, 26};
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
	const qreal radius = track.height() / 2.0;
	QColor trackColor = isChecked() ? on_ : off_;
	if (!isEnabled())
		trackColor.setAlphaF(0.4);

	p.setPen(Qt::NoPen);
	p.setBrush(trackColor);
	p.drawRoundedRect(track, radius, radius);

	const qreal knobSize = track.height() - 6;
	const qreal knobX = isChecked() ? track.right() - knobSize - 3 : track.left() + 3;
	p.setBrush(knob_);
	p.drawEllipse(QRectF(knobX, track.top() + 3, knobSize, knobSize));

	QFont f = font();
	f.setPixelSize(10);
	f.setBold(true);
	p.setFont(f);
	p.setPen(isChecked() ? QColor(Qt::white) : knob_);
	const QRectF textRect =
		isChecked() ? QRectF(track.left() + 8, track.top(), track.width() - knobSize - 14, track.height())
			    : QRectF(knobX + knobSize + 4, track.top(), track.right() - knobX - knobSize - 8,
				     track.height());
	p.drawText(textRect, Qt::AlignCenter, isChecked() ? QStringLiteral("ON") : QStringLiteral("OFF"));
}

} // namespace nvoice::ui
