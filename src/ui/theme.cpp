#include "theme.hpp"

namespace nvoice::ui {

namespace {

// Dark: charcoal / navy base with a blue accent.
const Palette kDark{
	QColor("#161A22"), // background
	QColor("#1E2330"), // surface
	QColor("#252B3A"), // surfaceAlt
	QColor("#2E3547"), // border
	QColor("#E7EAF2"), // text
	QColor("#98A1B5"), // textMuted
	QColor("#4C8DFF"), // accent
	QColor("#1F3358"), // accentSoft
	QColor("#3DDC84"), // success
	QColor("#2A3142"), // meterTrack
	QColor("#2FC7A0"), // meterFillStart
	QColor("#4BE38C"), // meterFillEnd
	QColor("#FF6B5E"), // meterHot
	QColor("#3A4256"), // toggleOff
};

// Light: white base with lavender / pink accents.
const Palette kLight{
	QColor("#FFFFFF"), // background
	QColor("#FBF8FD"), // surface
	QColor("#F4EEF9"), // surfaceAlt
	QColor("#E9DFF2"), // border
	QColor("#2B2533"), // text
	QColor("#7C7389"), // textMuted
	QColor("#E0559A"), // accent
	QColor("#FBE3EF"), // accentSoft
	QColor("#22A55B"), // success
	QColor("#ECE4F3"), // meterTrack
	QColor("#B596F0"), // meterFillStart
	QColor("#EC5FA3"), // meterFillEnd
	QColor("#E5484D"), // meterHot
	QColor("#D9CFE3"), // toggleOff
};

QString hex(const QColor &c)
{
	return c.name(QColor::HexRgb);
}

} // namespace

const Palette &paletteFor(config::Theme theme)
{
	return theme == config::Theme::Light ? kLight : kDark;
}

QString styleSheetFor(config::Theme theme)
{
	const Palette &p = paletteFor(theme);

	// Object names are set in dashboard_dock.cpp / feature_row.cpp.
	QString qss = QStringLiteral(R"(
#NVoiceRoot, #NVoiceRoot QScrollArea, #NVoiceContent {
	background: @background;
	color: @text;
	font-family: "Segoe UI", "Noto Sans JP", sans-serif;
	font-size: 13px;
}
#NVoiceRoot QLabel { color: @text; background: transparent; }
#NVoiceRoot QLabel[muted="true"] { color: @textMuted; font-size: 12px; }

#NVoiceTitle { font-size: 18px; font-weight: 600; }
#NVoiceSubtitle { color: @textMuted; font-size: 11px; letter-spacing: 0.5px; }
#NVoiceSectionTitle { font-size: 14px; font-weight: 600; }

#NVoiceStatusBadge {
	border-radius: 10px; padding: 2px 10px; font-size: 11px; font-weight: 600;
}
#NVoiceStatusBadge[state="active"] { background: @success; color: #FFFFFF; }
#NVoiceStatusBadge[state="inactive"] { background: @surfaceAlt; color: @textMuted; }

#NVoiceThemeSwitch {
	background: @surfaceAlt; border: 1px solid @border; border-radius: 15px;
}
#NVoiceThemeSwitch QPushButton {
	background: transparent; border: none; border-radius: 12px;
	min-width: 34px; min-height: 24px; color: @textMuted; font-size: 14px;
}
#NVoiceThemeSwitch QPushButton:checked { background: @accent; color: #FFFFFF; }

QPushButton#NVoicePreset {
	background: @surface; color: @text;
	border: 1px solid @border; border-radius: 8px;
	padding: 8px 6px; font-weight: 600;
}
QPushButton#NVoicePreset:hover { border-color: @accent; }
QPushButton#NVoicePreset:checked {
	background: @accentSoft; border: 2px solid @accent; color: @text;
}

#NVoiceCard {
	background: @surface; border: 1px solid @border; border-radius: 10px;
}
#NVoiceLevelValue { font-size: 22px; font-weight: 600; }
#NVoiceHint { color: @textMuted; font-size: 12px; }

#NVoiceChip {
	border-radius: 10px; padding: 2px 10px; font-size: 11px; font-weight: 600;
}
#NVoiceChip[state="ok"] { background: @accentSoft; color: @success; }
#NVoiceChip[state="idle"] { background: @surfaceAlt; color: @textMuted; }
#NVoiceChip[state="off"] { background: @surfaceAlt; color: @meterHot; }

#NVoiceSourceCombo {
	background: @surfaceAlt; color: @text; border: 1px solid @border;
	border-radius: 6px; padding: 2px 8px; min-height: 22px;
}
#NVoiceSourceCombo QAbstractItemView {
	background: @surface; color: @text; selection-background-color: @accentSoft;
}

#NVoiceFeatureList {
	background: @surface; border: 1px solid @border; border-radius: 10px;
}
#NVoiceFeatureRow { background: transparent; border: none; border-bottom: 1px solid @border; }
#NVoiceFeatureRow[last="true"] { border-bottom: none; }
#NVoiceFeatureIcon {
	background: @surfaceAlt; border-radius: 8px; color: @accent;
	min-width: 34px; max-width: 34px; min-height: 34px; max-height: 34px;
	font-size: 16px;
}
#NVoiceFeatureTitle { font-weight: 600; }
#NVoiceFeatureRow[comingSoon="true"] #NVoiceFeatureTitle { color: @textMuted; }
#NVoiceFeatureRow[comingSoon="true"] #NVoiceFeatureIcon { color: @textMuted; }
#NVoiceComingSoon {
	background: @surfaceAlt; color: @textMuted; border: 1px dashed @border;
	border-radius: 9px; padding: 2px 8px; font-size: 10px; font-weight: 600;
}
#NVoiceChevron { color: @textMuted; font-size: 16px; }
#NVoiceFeatureRow[comingSoon="true"] #NVoiceChevron { color: @border; }

#NVoiceRoot QScrollBar:vertical { background: transparent; width: 8px; }
#NVoiceRoot QScrollBar::handle:vertical { background: @border; border-radius: 4px; min-height: 24px; }
#NVoiceRoot QScrollBar::add-line, #NVoiceRoot QScrollBar::sub-line { height: 0; }
)");

	const std::pair<const char *, QColor> tokens[] = {
		{"@background", p.background}, {"@surfaceAlt", p.surfaceAlt}, {"@surface", p.surface},
		{"@border", p.border},         {"@textMuted", p.textMuted},   {"@text", p.text},
		{"@accentSoft", p.accentSoft}, {"@accent", p.accent},         {"@success", p.success},
		{"@meterHot", p.meterHot},
	};
	// Longer tokens first (e.g. @surfaceAlt before @surface) — order above matters.
	for (const auto &[token, color] : tokens)
		qss.replace(QString::fromLatin1(token), hex(color));
	return qss;
}

} // namespace nvoice::ui
