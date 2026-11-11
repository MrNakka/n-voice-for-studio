// N Voice for Studio - UI
// Feature detail page (requirements §5.3): title and short description,
// ON/OFF, the main controls, a collapsible "詳細設定" area, a live meter,
// and reset.
#pragma once

#include "param_control.hpp"

#include <QColor>
#include <QFrame>
#include <QVector>

class QLabel;
class QToolButton;
class QWidget;

namespace nvoice::ui {

class ToggleSwitch;

// Horizontal bar for a gain value in dB. Fills from the 0 dB point, so it
// works for reduction (0 .. -24) and for boost/cut (-12 .. +18) alike.
class GainBar : public QWidget {
public:
	GainBar(float minDb, float maxDb, QWidget *parent = nullptr);
	void setValue(float db);
	void setColors(const QColor &track, const QColor &fill);
	QSize sizeHint() const override { return {200, 10}; }

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	float min_;
	float max_;
	float value_ = 0.0f;
	QColor track_{"#2A3142"};
	QColor fill_{"#4C8DFF"};
};

class FeaturePage : public QFrame {
	Q_OBJECT

public:
	struct Spec {
		QString id;
		QString title;
		QString description;
		QString meterLabel; // e.g. "いま抑えている量"
		float meterMinDb;
		float meterMaxDb;
		QVector<ParamControl::Spec> basic;
		QVector<ParamControl::Spec> advanced;
	};

	explicit FeaturePage(const Spec &spec, QWidget *parent = nullptr);

	const QString &featureId() const { return spec_.id; }

	// Display only; none of these emit signals.
	void setEnabledState(bool enabled);
	void setValue(const QString &paramId, double value);
	void setMeter(float db);
	void setControlsEnabled(bool enabled);

	void applyPalette(const QColor &accent, const QColor &toggleOff, const QColor &track);

	// Shows `widget` right below the live meter (e.g. the waveform).
	void setExtraWidget(QWidget *widget);

	const QVector<ParamControl *> &controls() const { return controls_; }

signals:
	void backRequested();
	void enabledToggled(const QString &featureId, bool enabled);
	void valueChanged(const QString &paramId, double value);
	void resetRequested(const QString &featureId);

private:
	Spec spec_;
	ToggleSwitch *toggle_ = nullptr;
	QVector<ParamControl *> controls_;
	QLabel *meterValue_ = nullptr;
	GainBar *meterBar_ = nullptr;
	QWidget *advancedArea_ = nullptr;
	QFrame *extraCard_ = nullptr;
	QToolButton *advancedButton_ = nullptr;
};

} // namespace nvoice::ui
