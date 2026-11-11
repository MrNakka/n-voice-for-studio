#include "feature_row.hpp"

#include "toggle_switch.hpp"

#include <obs-module.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace nvoice::ui {

FeatureRow::FeatureRow(const Spec &spec, QWidget *parent) : QFrame(parent), spec_(spec)
{
	setObjectName(QStringLiteral("NVoiceFeatureRow"));
	setProperty("comingSoon", !spec.available);
	setProperty("last", false);
	setAttribute(Qt::WA_StyledBackground, true);
	if (spec.available)
		setCursor(Qt::PointingHandCursor);

	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(12, 10, 12, 10);
	layout->setSpacing(12);

	auto *icon = new QLabel(spec.icon, this);
	icon->setObjectName(QStringLiteral("NVoiceFeatureIcon"));
	icon->setAlignment(Qt::AlignCenter);
	layout->addWidget(icon);

	auto *texts = new QVBoxLayout();
	texts->setSpacing(2);
	auto *title = new QLabel(spec.title, this);
	title->setObjectName(QStringLiteral("NVoiceFeatureTitle"));
	subtitle_ = new QLabel(spec.subtitle, this);
	subtitle_->setProperty("muted", true);
	subtitle_->setWordWrap(true);
	texts->addWidget(title);
	texts->addWidget(subtitle_);
	layout->addLayout(texts, 1);

	if (!spec.available) {
		auto *badge = new QLabel(QString::fromUtf8(obs_module_text("NVoice.ComingSoon")), this);
		badge->setObjectName(QStringLiteral("NVoiceComingSoon"));
		layout->addWidget(badge, 0, Qt::AlignVCenter);
		setToolTip(QString::fromUtf8(obs_module_text("NVoice.ComingSoon.Tooltip")));
	} else if (spec.hasToggle) {
		toggle_ = new ToggleSwitch(this);
		layout->addWidget(toggle_, 0, Qt::AlignVCenter);
		connect(toggle_, &ToggleSwitch::toggled, this, [this](bool on) { emit toggled(spec_.id, on); });
	}

	auto *chevron = new QLabel(QStringLiteral("›"), this);
	chevron->setObjectName(QStringLiteral("NVoiceChevron"));
	layout->addWidget(chevron, 0, Qt::AlignVCenter);
}

void FeatureRow::setSubtitle(const QString &text)
{
	subtitle_->setText(text);
}

void FeatureRow::setChecked(bool checked)
{
	if (toggle_) {
		const QSignalBlocker blocker(toggle_);
		toggle_->setChecked(checked);
	}
}

void FeatureRow::setInteractive(bool interactive)
{
	interactive_ = interactive;
	if (toggle_)
		toggle_->setEnabled(interactive);
	if (spec_.available)
		setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

bool FeatureRow::isChecked() const
{
	return toggle_ && toggle_->isChecked();
}

void FeatureRow::setLast(bool last)
{
	setProperty("last", last);
}

void FeatureRow::mouseReleaseEvent(QMouseEvent *event)
{
	// Coming Soon rows are inert: no toggling, no navigation.
	if (spec_.available && interactive_ && event->button() == Qt::LeftButton)
		emit openRequested(spec_.id);
	QFrame::mouseReleaseEvent(event);
}

} // namespace nvoice::ui
