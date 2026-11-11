// N Voice for Studio - UI
// Dark / Light theme palettes (requirements v0.2 §5.2). Applied only to the
// N Voice dock, never to the rest of OBS.
#pragma once

#include "../config/app_settings.hpp"

#include <QColor>
#include <QString>

namespace nvoice::ui {

struct Palette {
	QColor background;
	QColor surface;
	QColor surfaceAlt;
	QColor border;
	QColor text;
	QColor textMuted;
	QColor accent;
	QColor accentSoft;
	QColor success;
	QColor meterTrack;
	// OBS meter zones: below -20 / -20..-9 / above -9 dBFS
	QColor meterGreen;
	QColor meterYellow;
	QColor meterRed;
	QColor toggleOff;
};

const Palette &paletteFor(config::Theme theme);

// Style sheet for the dock's root widget.
QString styleSheetFor(config::Theme theme);

} // namespace nvoice::ui
