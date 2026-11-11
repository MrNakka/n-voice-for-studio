// N Voice for Studio - UI
// Pill-shaped ON/OFF switch.
#pragma once

#include <QAbstractButton>
#include <QColor>

namespace nvoice::ui {

class ToggleSwitch : public QAbstractButton {
	Q_OBJECT

public:
	explicit ToggleSwitch(QWidget *parent = nullptr);

	void setColors(const QColor &on, const QColor &off, const QColor &knob);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QColor on_{"#4C8DFF"};
	QColor off_{"#3A4256"};
	QColor knob_{"#FFFFFF"};
};

} // namespace nvoice::ui
