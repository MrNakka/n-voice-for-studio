#include "feature_page.hpp"

#include "toggle_switch.hpp"

#include <obs-module.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace nvoice::ui {

namespace {

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

} // namespace

// ---------------------------------------------------------------------------
// GainBar
// ---------------------------------------------------------------------------

GainBar::GainBar(float minDb, float maxDb, QWidget *parent) : QWidget(parent), min_(minDb), max_(maxDb)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setMinimumHeight(10);
}

void GainBar::setValue(float db)
{
	value_ = std::clamp(db, min_, max_);
	update();
}

void GainBar::setColors(const QColor &track, const QColor &fill)
{
	track_ = track;
	fill_ = fill;
	update();
}

void GainBar::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);

	const QRectF bar = QRectF(rect()).adjusted(0, 1, 0, -1);
	const qreal radius = bar.height() / 2.0;
	p.setBrush(track_);
	p.drawRoundedRect(bar, radius, radius);

	auto xFor = [&](float db) {
		return bar.left() + bar.width() * (db - min_) / (max_ - min_);
	};
	const qreal zero = xFor(0.0f);
	const qreal x = xFor(value_);
	const QRectF fill(std::min(zero, x), bar.top(), std::max(std::abs(x - zero), 2.0), bar.height());
	p.setBrush(fill_);
	p.drawRoundedRect(fill, radius, radius);

	// 0 dB tick when the bar spans both directions.
	if (min_ < 0.0f && max_ > 0.0f) {
		QColor tick = fill_;
		tick.setAlphaF(0.6f);
		p.setBrush(tick);
		p.drawRect(QRectF(zero - 0.5, bar.top() - 1, 1.0, bar.height() + 2));
	}
}

// ---------------------------------------------------------------------------
// FeaturePage
// ---------------------------------------------------------------------------

FeaturePage::FeaturePage(const Spec &spec, QWidget *parent) : QFrame(parent), spec_(spec)
{
	setObjectName(QStringLiteral("NVoiceFeaturePage"));

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(14);

	// Header: back, title, ON/OFF
	auto *header = new QHBoxLayout();
	header->setSpacing(8);
	auto *back = new QPushButton(QStringLiteral("‹  ") + T("NVoice.Page.Back"), this);
	back->setObjectName(QStringLiteral("NVoiceBack"));
	back->setCursor(Qt::PointingHandCursor);
	connect(back, &QPushButton::clicked, this, &FeaturePage::backRequested);
	header->addWidget(back);
	header->addStretch(1);
	toggle_ = new ToggleSwitch(this);
	connect(toggle_, &ToggleSwitch::toggled, this, [this](bool on) { emit enabledToggled(spec_.id, on); });
	header->addWidget(toggle_);
	layout->addLayout(header);

	auto *title = new QLabel(spec.title, this);
	title->setObjectName(QStringLiteral("NVoiceTitle"));
	layout->addWidget(title);
	auto *description = new QLabel(spec.description, this);
	description->setObjectName(QStringLiteral("NVoiceHint"));
	description->setWordWrap(true);
	layout->addWidget(description);

	// Live meter
	auto *meterCard = new QFrame(this);
	meterCard->setObjectName(QStringLiteral("NVoiceCard"));
	auto *meterLayout = new QVBoxLayout(meterCard);
	meterLayout->setContentsMargins(14, 10, 14, 12);
	meterLayout->setSpacing(6);
	auto *meterRow = new QHBoxLayout();
	auto *meterLabel = new QLabel(spec.meterLabel, meterCard);
	meterValue_ = new QLabel(QStringLiteral("0.0 dB"), meterCard);
	meterValue_->setObjectName(QStringLiteral("NVoiceMeterValue"));
	meterRow->addWidget(meterLabel);
	meterRow->addStretch(1);
	meterRow->addWidget(meterValue_);
	meterLayout->addLayout(meterRow);
	meterBar_ = new GainBar(spec.meterMinDb, spec.meterMaxDb, meterCard);
	meterLayout->addWidget(meterBar_);
	layout->addWidget(meterCard);

	auto addControls = [this](const QVector<ParamControl::Spec> &specs, QVBoxLayout *target, QWidget *owner) {
		for (const auto &s : specs) {
			auto *control = new ParamControl(s, owner);
			connect(control, &ParamControl::valueChanged, this, &FeaturePage::valueChanged);
			target->addWidget(control);
			controls_.push_back(control);
		}
	};

	// Basic controls
	auto *basicCard = new QFrame(this);
	basicCard->setObjectName(QStringLiteral("NVoiceCard"));
	auto *basicLayout = new QVBoxLayout(basicCard);
	basicLayout->setContentsMargins(14, 12, 14, 14);
	basicLayout->setSpacing(14);
	addControls(spec.basic, basicLayout, basicCard);
	layout->addWidget(basicCard);

	// Advanced (collapsed by default)
	if (!spec.advanced.isEmpty()) {
		advancedButton_ = new QToolButton(this);
		advancedButton_->setObjectName(QStringLiteral("NVoiceDisclosure"));
		advancedButton_->setText(T("NVoice.Page.Advanced"));
		advancedButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		advancedButton_->setArrowType(Qt::RightArrow);
		advancedButton_->setCheckable(true);
		advancedButton_->setCursor(Qt::PointingHandCursor);
		layout->addWidget(advancedButton_);

		advancedArea_ = new QFrame(this);
		advancedArea_->setObjectName(QStringLiteral("NVoiceCard"));
		auto *advLayout = new QVBoxLayout(advancedArea_);
		advLayout->setContentsMargins(14, 12, 14, 14);
		advLayout->setSpacing(14);
		addControls(spec.advanced, advLayout, advancedArea_);
		advancedArea_->setVisible(false);
		layout->addWidget(advancedArea_);

		connect(advancedButton_, &QToolButton::toggled, this, [this](bool open) {
			advancedArea_->setVisible(open);
			advancedButton_->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
		});
	}

	auto *footer = new QHBoxLayout();
	footer->addStretch(1);
	auto *reset = new QPushButton(T("NVoice.Page.Reset"), this);
	reset->setObjectName(QStringLiteral("NVoiceSecondary"));
	reset->setCursor(Qt::PointingHandCursor);
	reset->setToolTip(T("NVoice.Page.Reset.Tooltip"));
	connect(reset, &QPushButton::clicked, this, [this] { emit resetRequested(spec_.id); });
	footer->addWidget(reset);
	layout->addLayout(footer);
	layout->addStretch(1);
}

void FeaturePage::setEnabledState(bool enabled)
{
	const QSignalBlocker blocker(toggle_);
	toggle_->setChecked(enabled);
}

void FeaturePage::setValue(const QString &paramId, double value)
{
	for (auto *control : controls_) {
		if (QString::fromUtf8(control->param().id.data()) == paramId)
			control->setValue(value);
	}
}

void FeaturePage::setMeter(float db)
{
	meterValue_->setText(
		QStringLiteral("%1%2 dB").arg(db > 0.05f ? QStringLiteral("+") : QString()).arg(db, 0, 'f', 1));
	meterBar_->setValue(db);
}

void FeaturePage::setControlsEnabled(bool enabled)
{
	for (auto *control : controls_)
		control->setEnabled(enabled);
	toggle_->setEnabled(enabled);
}

void FeaturePage::applyPalette(const QColor &accent, const QColor &toggleOff, const QColor &track)
{
	toggle_->setColors(accent, toggleOff, QColor(Qt::white));
	meterBar_->setColors(track, accent);
}

} // namespace nvoice::ui
