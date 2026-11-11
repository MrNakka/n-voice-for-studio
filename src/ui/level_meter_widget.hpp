// N Voice for Studio - UI
// Horizontal level bar: smoothed RMS fill plus a peak-hold marker.
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

	// rmsDb / peakDb in dBFS (values <= -120 mean silence).
	void setLevels(float rmsDb, float peakDb);
	void clear();

	// Peak held for display (holds ~1.5 s, then falls).
	float heldPeakDb() const { return heldPeakDb_; }

	void setColors(const QColor &track, const QColor &fillStart, const QColor &fillEnd, const QColor &hot);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	static constexpr float kHotDb = -6.0f;

	float toFraction(float db) const;

	float rmsDb_ = -120.0f;
	float heldPeakDb_ = -120.0f;
	QElapsedTimer peakTimer_;
	QElapsedTimer frameTimer_;

	QColor track_{"#2A3142"};
	QColor fillStart_{"#2FC7A0"};
	QColor fillEnd_{"#4BE38C"};
	QColor hot_{"#FF6B5E"};
};

} // namespace nvoice::ui
