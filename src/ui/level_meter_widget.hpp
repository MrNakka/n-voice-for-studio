// N Voice for Studio - UI
// Level bar that reads like OBS Studio's mixer meter, so both can be
// compared side by side: sample peak, instant attack, 23.53 dB/s decay
// (OBS default "Fast"), green / yellow / red zones at -20 / -9 dBFS, plus a
// held peak marker.
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QWidget>

namespace nvoice::ui {

class LevelMeterWidget : public QWidget {
	Q_OBJECT

public:
	// Lowest level shown. Below this the bar is empty and the UI shows -inf.
	static constexpr float kFloorDb = -60.0f;

	explicit LevelMeterWidget(QWidget *parent = nullptr);

	// peakDb: highest sample peak since the previous call (dBFS). rmsDb is
	// accepted for API stability but the bar follows the peak, like OBS.
	void setLevels(float rmsDb, float peakDb);
	void clear();

	// What the bar shows right now (OBS-style meter reading).
	float displayDb() const { return displayDb_; }
	// Peak held for display (holds ~1.5 s, then falls).
	float heldPeakDb() const { return heldPeakDb_; }

	void setColors(const QColor &track, const QColor &green, const QColor &yellow, const QColor &red);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	float toFraction(float db) const;

	float displayDb_ = -120.0f;
	float heldPeakDb_ = -120.0f;
	QElapsedTimer peakTimer_;
	QElapsedTimer frameTimer_;

	QColor track_{"#2A3142"};
	QColor green_{"#3DDC84"};
	QColor yellow_{"#F2C94C"};
	QColor red_{"#FF5A52"};
};

} // namespace nvoice::ui
