// N Voice for Studio - UI
// "声に合わせて自動設定": the user reads three sample lines (normal, quieter,
// louder) for a few seconds; the page then shows what was measured and the
// proposed settings, which are applied only when the user confirms.
#pragma once

#include "../config/app_settings.hpp"

#include <nvoice/engine/voice_analyzer.hpp>

#include <QColor>
#include <QFrame>
#include <QVector>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace nvoice::ui {

class GainBar;
class LevelMeterWidget;

class CalibrationPage : public QFrame {
	Q_OBJECT

public:
	static constexpr double kDurationSec = 10.0;

	// Current values, shown next to the proposal ("今 → 提案").
	struct Current {
		float gateDb;
		float maxGainDb;
		float maxCutDb;
		float compStrength;
	};

	explicit CalibrationPage(QWidget *parent = nullptr);

	// Loads the sample lines for the selected stream style.
	void setProfile(config::Profile profile, const QString &profileName);

	void showIntro();
	void showRecording();
	void setProgress(double elapsedSec);
	void setLevel(float rmsDb, float peakDb);
	void showResult(const engine::CalibrationResult &result, const Current &current);

	void applyPalette(const QColor &accent, const QColor &track, const QColor &green, const QColor &yellow,
			  const QColor &red);

signals:
	void backRequested();
	void startRequested();
	void cancelRequested();
	void applyRequested();

private:
	void setActiveLine(int index); // -1: none

	QLabel *intro_ = nullptr;
	QString profileName_;
	QVector<QFrame *> lines_;
	QVector<QLabel *> guides_;
	QVector<QLabel *> texts_;

	QFrame *recordingArea_ = nullptr;
	GainBar *progress_ = nullptr;
	QLabel *remaining_ = nullptr;
	LevelMeterWidget *meter_ = nullptr;

	QFrame *resultArea_ = nullptr;
	QVBoxLayout *resultLayout_ = nullptr;

	QPushButton *start_ = nullptr;
	QPushButton *cancel_ = nullptr;
	QPushButton *retry_ = nullptr;
	QPushButton *apply_ = nullptr;
};

} // namespace nvoice::ui
