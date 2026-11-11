#include "calibration_page.hpp"

#include "feature_page.hpp"
#include "level_meter_widget.hpp"

#include <nvoice/engine/db.hpp>

#include <obs-module.h>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <cmath>

namespace nvoice::ui {

namespace {

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString TP(const char *profileKey, const char *suffix)
{
	const QByteArray key = QByteArray("NVoice.Calib.") + profileKey + "." + suffix;
	return T(key.constData());
}

const char *profileKey(config::Profile profile)
{
	switch (profile) {
	case config::Profile::Game:
		return "Game";
	case config::Profile::Singing:
		return "Singing";
	case config::Profile::LateNight:
		return "LateNight";
	case config::Profile::Chat:
	default:
		return "Chat";
	}
}

QString db(float v, int decimals = 0)
{
	if (v <= engine::kSilenceDb + 1.0f)
		return QStringLiteral("-∞ dB");
	return QStringLiteral("%1 dB").arg(static_cast<double>(v), 0, 'f', decimals);
}

void repolish(QWidget *w)
{
	w->style()->unpolish(w);
	w->style()->polish(w);
}

void clearLayout(QLayout *layout)
{
	while (QLayoutItem *item = layout->takeAt(0)) {
		if (QWidget *w = item->widget())
			w->deleteLater();
		else if (QLayout *child = item->layout())
			clearLayout(child);
		delete item;
	}
}

} // namespace

CalibrationPage::CalibrationPage(QWidget *parent) : QFrame(parent)
{
	setObjectName(QStringLiteral("NVoiceFeaturePage"));
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(14);

	auto *back = new QPushButton(QStringLiteral("‹  ") + T("NVoice.Page.Back"), this);
	back->setObjectName(QStringLiteral("NVoiceBack"));
	back->setCursor(Qt::PointingHandCursor);
	connect(back, &QPushButton::clicked, this, [this] {
		emit cancelRequested();
		emit backRequested();
	});
	auto *header = new QHBoxLayout();
	header->addWidget(back);
	header->addStretch(1);
	layout->addLayout(header);

	auto *title = new QLabel(T("NVoice.Calib.Title"), this);
	title->setObjectName(QStringLiteral("NVoiceTitle"));
	layout->addWidget(title);

	intro_ = new QLabel(this);
	intro_->setObjectName(QStringLiteral("NVoiceHint"));
	intro_->setWordWrap(true);
	layout->addWidget(intro_);

	// Sample lines
	auto *script = new QFrame(this);
	script->setObjectName(QStringLiteral("NVoiceCard"));
	auto *scriptLayout = new QVBoxLayout(script);
	scriptLayout->setContentsMargins(10, 10, 10, 10);
	scriptLayout->setSpacing(6);
	for (int i = 0; i < 3; ++i) {
		auto *line = new QFrame(script);
		line->setObjectName(QStringLiteral("NVoiceScriptLine"));
		line->setProperty("active", false);
		auto *lineLayout = new QVBoxLayout(line);
		lineLayout->setContentsMargins(10, 6, 10, 8);
		lineLayout->setSpacing(2);
		auto *guide = new QLabel(line);
		guide->setObjectName(QStringLiteral("NVoiceScriptGuide"));
		auto *text = new QLabel(line);
		text->setObjectName(QStringLiteral("NVoiceScriptText"));
		text->setWordWrap(true);
		lineLayout->addWidget(guide);
		lineLayout->addWidget(text);
		scriptLayout->addWidget(line);
		lines_.push_back(line);
		guides_.push_back(guide);
		texts_.push_back(text);
	}
	layout->addWidget(script);

	// Recording progress and live level
	recordingArea_ = new QFrame(this);
	recordingArea_->setObjectName(QStringLiteral("NVoiceCard"));
	auto *recLayout = new QVBoxLayout(recordingArea_);
	recLayout->setContentsMargins(14, 10, 14, 12);
	recLayout->setSpacing(8);
	auto *recRow = new QHBoxLayout();
	auto *recLabel = new QLabel(QStringLiteral("● ") + T("NVoice.Calib.Recording"), recordingArea_);
	recLabel->setObjectName(QStringLiteral("NVoiceRecording"));
	remaining_ = new QLabel(recordingArea_);
	recRow->addWidget(recLabel);
	recRow->addStretch(1);
	recRow->addWidget(remaining_);
	recLayout->addLayout(recRow);
	progress_ = new GainBar(0.0f, static_cast<float>(kDurationSec), recordingArea_);
	recLayout->addWidget(progress_);
	meter_ = new LevelMeterWidget(recordingArea_);
	recLayout->addWidget(meter_);
	layout->addWidget(recordingArea_);

	// Result
	resultArea_ = new QFrame(this);
	resultLayout_ = new QVBoxLayout(resultArea_);
	resultLayout_->setContentsMargins(0, 0, 0, 0);
	resultLayout_->setSpacing(10);
	layout->addWidget(resultArea_);

	// Buttons
	auto *buttons = new QHBoxLayout();
	buttons->addStretch(1);
	cancel_ = new QPushButton(T("NVoice.Calib.Cancel"), this);
	cancel_->setObjectName(QStringLiteral("NVoiceSecondary"));
	retry_ = new QPushButton(T("NVoice.Calib.Retry"), this);
	retry_->setObjectName(QStringLiteral("NVoiceSecondary"));
	start_ = new QPushButton(T("NVoice.Calib.Start"), this);
	start_->setObjectName(QStringLiteral("NVoicePrimary"));
	apply_ = new QPushButton(T("NVoice.Calib.Apply"), this);
	apply_->setObjectName(QStringLiteral("NVoicePrimary"));
	for (auto *b : {cancel_, retry_, start_, apply_}) {
		b->setCursor(Qt::PointingHandCursor);
		buttons->addWidget(b);
	}
	layout->addLayout(buttons);
	layout->addStretch(1);

	connect(start_, &QPushButton::clicked, this, &CalibrationPage::startRequested);
	connect(retry_, &QPushButton::clicked, this, &CalibrationPage::startRequested);
	connect(apply_, &QPushButton::clicked, this, &CalibrationPage::applyRequested);
	connect(cancel_, &QPushButton::clicked, this, [this] {
		emit cancelRequested();
		emit backRequested();
	});

	showIntro();
}

void CalibrationPage::setProfile(config::Profile profile, const QString &profileName)
{
	const char *key = profileKey(profile);
	const char *guideKeys[] = {"Guide1", "Guide2", "Guide3"};
	const char *lineKeys[] = {"Line1", "Line2", "Line3"};
	for (int i = 0; i < 3; ++i) {
		guides_[i]->setText(QStringLiteral("%1. %2").arg(i + 1).arg(TP(key, guideKeys[i])));
		texts_[i]->setText(TP(key, lineKeys[i]));
	}
	profileName_ = profileName;
	intro_->setText(T("NVoice.Calib.Intro").arg(profileName).arg(static_cast<int>(kDurationSec)));
}

void CalibrationPage::showIntro()
{
	setActiveLine(-1);
	recordingArea_->setVisible(false);
	resultArea_->setVisible(false);
	start_->setVisible(true);
	cancel_->setVisible(true);
	retry_->setVisible(false);
	apply_->setVisible(false);
}

void CalibrationPage::showRecording()
{
	recordingArea_->setVisible(true);
	resultArea_->setVisible(false);
	start_->setVisible(false);
	retry_->setVisible(false);
	apply_->setVisible(false);
	cancel_->setVisible(true);
	meter_->clear();
	setProgress(0.0);
}

void CalibrationPage::setProgress(double elapsedSec)
{
	progress_->setValue(static_cast<float>(elapsedSec));
	const int left = std::max(0, static_cast<int>(std::ceil(kDurationSec - elapsedSec)));
	remaining_->setText(T("NVoice.Calib.Remaining").arg(left));
	// Each line gets a third of the time.
	setActiveLine(std::min(2, static_cast<int>(elapsedSec / (kDurationSec / 3.0))));
}

void CalibrationPage::setLevel(float rmsDb, float peakDb)
{
	meter_->setLevels(rmsDb, peakDb);
}

void CalibrationPage::setActiveLine(int index)
{
	for (int i = 0; i < lines_.size(); ++i) {
		const bool active = i == index;
		if (lines_[i]->property("active").toBool() != active) {
			lines_[i]->setProperty("active", active);
			repolish(lines_[i]);
		}
	}
}

void CalibrationPage::showResult(const engine::CalibrationResult &r, const Current &current)
{
	setActiveLine(-1);
	recordingArea_->setVisible(false);
	clearLayout(resultLayout_);
	resultArea_->setVisible(true);
	start_->setVisible(false);
	cancel_->setVisible(true);
	retry_->setVisible(true);
	apply_->setVisible(r.usable);

	auto addMessage = [this](const QString &text, const char *kind) {
		auto *label = new QLabel(text, resultArea_);
		label->setObjectName(QStringLiteral("NVoiceMessage"));
		label->setProperty("kind", kind);
		label->setWordWrap(true);
		resultLayout_->addWidget(label);
	};

	if (!r.usable) {
		addMessage(T("NVoice.Calib.Issue.NotEnoughSpeech"), "warn");
		return;
	}

	// What was measured
	auto *measured = new QFrame(resultArea_);
	measured->setObjectName(QStringLiteral("NVoiceCard"));
	auto *grid = new QGridLayout(measured);
	grid->setContentsMargins(14, 10, 14, 12);
	grid->setHorizontalSpacing(12);
	grid->setVerticalSpacing(6);
	auto *mTitle = new QLabel(T("NVoice.Calib.Measured"), measured);
	mTitle->setObjectName(QStringLiteral("NVoiceSectionTitle"));
	grid->addWidget(mTitle, 0, 0, 1, 2);
	const auto &s = r.stats;
	const std::pair<const char *, QString> rows[] = {
		{"NVoice.Calib.M.Speech", db(s.speechPeakDb) + QStringLiteral("FS")},
		{"NVoice.Calib.M.Noise", db(s.noiseDb) + QStringLiteral("FS")},
		{"NVoice.Calib.M.Range", db(s.loudSpeechDb - s.quietSpeechDb)},
		{"NVoice.Calib.M.Time", T("NVoice.Calib.Seconds").arg(static_cast<double>(s.speechSeconds), 0, 'f', 1)},
	};
	int row = 1;
	for (const auto &[key, value] : rows) {
		auto *k = new QLabel(T(key), measured);
		k->setProperty("muted", true);
		auto *v = new QLabel(value, measured);
		v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		grid->addWidget(k, row, 0);
		grid->addWidget(v, row, 1);
		++row;
	}
	resultLayout_->addWidget(measured);

	if (r.issues & engine::kIssueTooQuiet)
		addMessage(T("NVoice.Calib.Issue.TooQuiet"), "warn");
	if (r.issues & engine::kIssueClipping)
		addMessage(T("NVoice.Calib.Issue.Clipping"), "warn");
	if (r.issues & engine::kIssueNoisy)
		addMessage(T("NVoice.Calib.Issue.Noisy"), "info");

	// Proposal: current -> proposed
	auto *proposal = new QFrame(resultArea_);
	proposal->setObjectName(QStringLiteral("NVoiceCard"));
	auto *pGrid = new QGridLayout(proposal);
	pGrid->setContentsMargins(14, 10, 14, 12);
	pGrid->setHorizontalSpacing(10);
	pGrid->setVerticalSpacing(6);
	auto *pTitle = new QLabel(T("NVoice.Calib.Proposal"), proposal);
	pTitle->setObjectName(QStringLiteral("NVoiceSectionTitle"));
	pGrid->addWidget(pTitle, 0, 0, 1, 3);

	auto pct = [](float v) {
		return QStringLiteral("%1 %").arg(static_cast<double>(v), 0, 'f', 0);
	};
	struct Change {
		const char *key;
		QString from;
		QString to;
		bool changed;
	};
	const auto &p = r.proposal;
	auto differs = [](float a, float b) {
		return std::fabs(a - b) >= 0.5f;
	};
	const Change changes[] = {
		{"NVoice.Param.Gate", db(current.gateDb), db(p.gateDb), differs(current.gateDb, p.gateDb)},
		{"NVoice.Param.MaxGain", db(current.maxGainDb), db(p.maxGainDb),
		 differs(current.maxGainDb, p.maxGainDb)},
		{"NVoice.Param.MaxCut", db(current.maxCutDb), db(p.maxCutDb), differs(current.maxCutDb, p.maxCutDb)},
		{"NVoice.Param.Strength", pct(current.compStrength), pct(p.compStrength),
		 differs(current.compStrength, p.compStrength)},
	};
	row = 1;
	for (const auto &c : changes) {
		auto *name = new QLabel(T(c.key), proposal);
		name->setWordWrap(true);
		auto *value = new QLabel(c.changed ? QStringLiteral("%1  →  <b>%2</b>").arg(c.from, c.to)
						   : T("NVoice.Calib.NoChange").arg(c.from),
					 proposal);
		value->setTextFormat(Qt::RichText);
		value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		pGrid->addWidget(name, row, 0);
		pGrid->addWidget(value, row, 1);
		pGrid->setColumnStretch(0, 1);
		++row;
	}
	auto *others = new QLabel(T("NVoice.Calib.Others").arg(profileName_), proposal);
	others->setProperty("muted", true);
	others->setWordWrap(true);
	pGrid->addWidget(others, row, 0, 1, 2);
	resultLayout_->addWidget(proposal);
	addMessage(T("NVoice.Calib.Note"), "note");
}

void CalibrationPage::applyPalette(const QColor &accent, const QColor &track, const QColor &green, const QColor &yellow,
				   const QColor &red)
{
	progress_->setColors(track, accent);
	meter_->setColors(track, green, yellow, red);
}

} // namespace nvoice::ui
