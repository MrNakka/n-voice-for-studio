#include "param_control.hpp"

#include <obs-module.h>

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace nvoice::ui {

namespace {

QString unitSuffix(engine::Unit unit)
{
	switch (unit) {
	case engine::Unit::Decibel:
		return QStringLiteral(" dB");
	case engine::Unit::Ratio:
		return QStringLiteral(" : 1");
	case engine::Unit::Milliseconds:
		return QStringLiteral(" ms");
	case engine::Unit::Seconds:
		return QStringLiteral(" ") + QString::fromUtf8(obs_module_text("NVoice.Unit.Seconds"));
	case engine::Unit::Percent:
	default:
		return QStringLiteral(" %");
	}
}

} // namespace

ParamControl::ParamControl(const Spec &spec, QWidget *parent) : QWidget(parent), spec_(spec)
{
	setObjectName(QStringLiteral("NVoiceParam"));
	setToolTip(spec.help);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);

	auto *label = new QLabel(spec.label, this);
	label->setObjectName(QStringLiteral("NVoiceParamLabel"));
	label->setWordWrap(true);
	layout->addWidget(label);

	auto *row = new QHBoxLayout();
	row->setSpacing(10);

	slider_ = new QSlider(Qt::Horizontal, this);
	slider_->setObjectName(QStringLiteral("NVoiceSlider"));
	slider_->setRange(0, toSlider(spec.param->max));
	row->addWidget(slider_, 1);

	spin_ = new QDoubleSpinBox(this);
	spin_->setObjectName(QStringLiteral("NVoiceSpin"));
	spin_->setRange(spec.param->min, spec.param->max); // input is range-limited
	spin_->setDecimals(spec.decimals);
	spin_->setSingleStep(spec.step);
	spin_->setSuffix(unitSuffix(spec.param->unit));
	spin_->setKeyboardTracking(false); // commit on Enter / focus out, not per keystroke
	spin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
	spin_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	spin_->setMinimumWidth(96);
	row->addWidget(spin_);
	layout->addLayout(row);

	setValue(spec.param->defaultValue);

	connect(slider_, &QSlider::valueChanged, this, [this](int position) {
		const double v = fromSlider(position);
		{
			const QSignalBlocker blocker(spin_);
			spin_->setValue(v);
		}
		emit valueChanged(QString::fromUtf8(spec_.param->id.data()), v);
	});
	connect(spin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
		{
			const QSignalBlocker blocker(slider_);
			slider_->setValue(toSlider(v));
		}
		emit valueChanged(QString::fromUtf8(spec_.param->id.data()), v);
	});
}

double ParamControl::value() const
{
	return spin_->value();
}

void ParamControl::setValue(double value)
{
	const double v = spec_.param->clamp(static_cast<float>(value));
	const QSignalBlocker b1(slider_);
	const QSignalBlocker b2(spin_);
	spin_->setValue(v);
	slider_->setValue(toSlider(v));
}

int ParamControl::toSlider(double value) const
{
	return static_cast<int>(std::lround((value - spec_.param->min) / spec_.step));
}

double ParamControl::fromSlider(int position) const
{
	return spec_.param->clamp(static_cast<float>(spec_.param->min + position * spec_.step));
}

} // namespace nvoice::ui
