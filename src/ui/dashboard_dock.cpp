#include "dashboard_dock.hpp"

#include "calibration_page.hpp"
#include "feature_page.hpp"
#include "feature_row.hpp"
#include "level_meter_widget.hpp"
#include "theme.hpp"
#include "toggle_switch.hpp"
#include "waveform_widget.hpp"
#include "../config/presets.hpp"
#include "../obs/filter_registry.hpp"
#include "../obs/filter_settings.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>

#include <QButtonGroup>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <vector>

namespace nvoice::ui {

namespace {

constexpr int kTickMs = 33;             // ~30 fps (requirements: 30-60 fps cap)
constexpr int kSourceRefreshTicks = 15; // ~0.5 s
constexpr float kSignalThresholdDb = LevelMeterWidget::kFloorDb;
constexpr qint64 kSignalHoldMs = 2000;
constexpr int kCompactPresetWidth = 470;

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// Matches the bar: anything below the meter floor reads as silence.
QString formatDb(float db)
{
	if (db < LevelMeterWidget::kFloorDb)
		return QStringLiteral("-∞ dBFS");
	return QStringLiteral("%1 dBFS").arg(static_cast<double>(db), 0, 'f', 1);
}

void repolish(QWidget *w)
{
	w->style()->unpolish(w);
	w->style()->polish(w);
	w->update();
}

QLabel *sectionTitle(const QString &glyph, const QString &text, QWidget *parent)
{
	auto *label = new QLabel(QStringLiteral("%1  %2").arg(glyph, text), parent);
	label->setObjectName(QStringLiteral("NVoiceSectionTitle"));
	return label;
}

} // namespace

DashboardDock::DashboardDock(QWidget *parent) : QFrame(parent)
{
	setObjectName(QStringLiteral("NVoiceRoot"));
	setAttribute(Qt::WA_StyledBackground, true);

	settings_ = store_.load();
	waveHistory_ = std::make_unique<WaveHistory>();
	buildUi();
	applyTheme(settings_.theme);
	setWaveformVisible(settings_.waveformVisible);

	clock_.start();
	refreshSources();
	connect(&timer_, &QTimer::timeout, this, &DashboardDock::tick);
	timer_.start(kTickMs);
}

DashboardDock::~DashboardDock()
{
	shutdown();
}

void DashboardDock::shutdown()
{
	timer_.stop();
	if (selected_) {
		selected_->chain.engine.setWaveformEnabled(false);
		selected_->chain.engine.setAnalysisEnabled(false);
	}
	selected_.reset();
	selectedFilter_ = nullptr;
}

QSize DashboardDock::sizeHint() const
{
	return {520, 720};
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void DashboardDock::buildUi()
{
	auto *rootLayout = new QVBoxLayout(this);
	rootLayout->setContentsMargins(0, 0, 0, 0);

	auto *scroll = new QScrollArea(this);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidgetResizable(true);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	rootLayout->addWidget(scroll);

	stack_ = new QStackedWidget(scroll);
	stack_->setObjectName(QStringLiteral("NVoiceContent"));
	stack_->setMinimumWidth(320); // stays operable in narrow docks
	scroll->setWidget(stack_);

	stack_->addWidget(buildDashboard());
	buildPages();

	// Only the visible page decides the scroll height.
	connect(stack_, &QStackedWidget::currentChanged, this, [this](int index) {
		for (int i = 0; i < stack_->count(); ++i) {
			const auto policy = i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored;
			stack_->widget(i)->setSizePolicy(policy, policy);
		}
		stack_->adjustSize();
	});
	for (int i = 1; i < stack_->count(); ++i)
		stack_->widget(i)->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	showDashboard();
}

QWidget *DashboardDock::buildDashboard()
{
	auto *page = new QWidget(stack_);
	page->setObjectName(QStringLiteral("NVoiceContent"));
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(16, 14, 16, 16);
	layout->setSpacing(14);

	layout->addWidget(buildHeader());
	layout->addWidget(buildPresets());
	layout->addWidget(buildMicCard());
	layout->addWidget(buildFeatureList());
	layout->addStretch(1);
	return page;
}

void DashboardDock::buildPages()
{
	namespace p = engine::params;
	auto pageHost = [this](FeaturePage *page) {
		auto *host = new QWidget(stack_);
		host->setObjectName(QStringLiteral("NVoiceContent"));
		auto *layout = new QVBoxLayout(host);
		layout->setContentsMargins(16, 14, 16, 16);
		layout->addWidget(page);
		stack_->addWidget(host);
		connect(page, &FeaturePage::backRequested, this, &DashboardDock::showDashboard);
		connect(page, &FeaturePage::enabledToggled, this, &DashboardDock::onFeatureToggled);
		connect(page, &FeaturePage::valueChanged, this, &DashboardDock::onParamChanged);
		connect(page, &FeaturePage::resetRequested, this, &DashboardDock::resetFeature);
	};

	FeaturePage::Spec comp;
	comp.id = QStringLiteral("compressor");
	comp.title = T("NVoice.Feature.Compressor.Title");
	comp.description = T("NVoice.Compressor.Description");
	comp.meterLabel = T("NVoice.Compressor.Meter");
	comp.meterMinDb = -24.0f;
	comp.meterMaxDb = 0.0f;
	comp.basic = {
		{&p::kCompStrength, T("NVoice.Param.Strength"), T("NVoice.Param.Strength.Help"), 0, 1.0},
		{&p::kCompMakeup, T("NVoice.Param.Makeup"), T("NVoice.Param.Makeup.Help"), 1, 0.5},
	};
	comp.advanced = {
		{&p::kCompThreshold, T("NVoice.Param.Threshold"), T("NVoice.Param.Threshold.Help"), 1, 0.5},
		{&p::kCompRatio, T("NVoice.Param.Ratio"), T("NVoice.Param.Ratio.Help"), 1, 0.1},
		{&p::kCompAttack, T("NVoice.Param.Attack"), T("NVoice.Param.Attack.Help"), 1, 0.5},
		{&p::kCompRelease, T("NVoice.Param.Release"), T("NVoice.Param.Release.Help"), 0, 5.0},
	};
	compressorPage_ = new FeaturePage(comp, stack_);
	compressorWave_ = new WaveformWidget(waveHistory_.get());
	compressorWave_->setToolTip(T("NVoice.Wave.Threshold.Help"));
	compressorPage_->setExtraWidget(compressorWave_);
	pageHost(compressorPage_);

	FeaturePage::Spec lev;
	lev.id = QStringLiteral("auto_leveler");
	lev.title = T("NVoice.Feature.AutoLeveler.Title");
	lev.description = T("NVoice.AutoLeveler.Description");
	lev.meterLabel = T("NVoice.AutoLeveler.Meter");
	lev.meterMinDb = -12.0f;
	lev.meterMaxDb = 18.0f;
	lev.basic = {
		{&p::kLevelerTarget, T("NVoice.Param.Target"), T("NVoice.Param.Target.Help"), 1, 0.5},
		{&p::kLevelerMaxGain, T("NVoice.Param.MaxGain"), T("NVoice.Param.MaxGain.Help"), 1, 0.5},
		{&p::kLevelerSpeed, T("NVoice.Param.Speed"), T("NVoice.Param.Speed.Help"), 1, 0.1},
	};
	lev.advanced = {
		{&p::kLevelerMaxCut, T("NVoice.Param.MaxCut"), T("NVoice.Param.MaxCut.Help"), 1, 0.5},
		{&p::kLevelerGate, T("NVoice.Param.Gate"), T("NVoice.Param.Gate.Help"), 1, 0.5},
	};
	levelerPage_ = new FeaturePage(lev, stack_);
	pageHost(levelerPage_);

	calibrationPage_ = new CalibrationPage(stack_);
	auto *calibHost = new QWidget(stack_);
	calibHost->setObjectName(QStringLiteral("NVoiceContent"));
	auto *calibLayout = new QVBoxLayout(calibHost);
	calibLayout->setContentsMargins(16, 14, 16, 16);
	calibLayout->addWidget(calibrationPage_);
	stack_->addWidget(calibHost);
	connect(calibrationPage_, &CalibrationPage::backRequested, this, &DashboardDock::showDashboard);
	connect(calibrationPage_, &CalibrationPage::startRequested, this, &DashboardDock::startCalibration);
	connect(calibrationPage_, &CalibrationPage::cancelRequested, this, &DashboardDock::stopCalibration);
	connect(calibrationPage_, &CalibrationPage::applyRequested, this, &DashboardDock::applyCalibration);
}

QWidget *DashboardDock::buildHeader()
{
	auto *header = new QWidget(this);
	auto *layout = new QHBoxLayout(header);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(10);

	auto *titles = new QVBoxLayout();
	titles->setSpacing(0);
	auto *title = new QLabel(QStringLiteral("N Voice for Studio"), header);
	title->setObjectName(QStringLiteral("NVoiceTitle"));
	auto *subtitle = new QLabel(
		QStringLiteral("AI AUDIO ENGINE FOR OBS  ·  v%1").arg(QString::fromUtf8(PLUGIN_VERSION)), header);
	subtitle->setObjectName(QStringLiteral("NVoiceSubtitle"));
	titles->addWidget(title);
	titles->addWidget(subtitle);
	layout->addLayout(titles, 1);

	statusBadge_ = new QLabel(header);
	statusBadge_->setObjectName(QStringLiteral("NVoiceStatusBadge"));
	layout->addWidget(statusBadge_, 0, Qt::AlignVCenter);

	// Two-way theme switch (Light / Dark). No "follow system" option (v0.2).
	auto *themeSwitch = new QFrame(header);
	themeSwitch->setObjectName(QStringLiteral("NVoiceThemeSwitch"));
	auto *themeLayout = new QHBoxLayout(themeSwitch);
	themeLayout->setContentsMargins(3, 3, 3, 3);
	themeLayout->setSpacing(2);
	lightButton_ = new QPushButton(QStringLiteral("☀"), themeSwitch);
	darkButton_ = new QPushButton(QStringLiteral("☾"), themeSwitch);
	lightButton_->setToolTip(T("NVoice.Theme.Light"));
	darkButton_->setToolTip(T("NVoice.Theme.Dark"));
	for (auto *b : {lightButton_, darkButton_}) {
		b->setCheckable(true);
		b->setCursor(Qt::PointingHandCursor);
		themeLayout->addWidget(b);
	}
	auto *themeGroup = new QButtonGroup(themeSwitch);
	themeGroup->setExclusive(true);
	themeGroup->addButton(lightButton_);
	themeGroup->addButton(darkButton_);
	(settings_.theme == config::Theme::Light ? lightButton_ : darkButton_)->setChecked(true);

	connect(lightButton_, &QPushButton::clicked, this, [this] {
		settings_.theme = config::Theme::Light;
		applyTheme(settings_.theme);
		persist();
	});
	connect(darkButton_, &QPushButton::clicked, this, [this] {
		settings_.theme = config::Theme::Dark;
		applyTheme(settings_.theme);
		persist();
	});
	layout->addWidget(themeSwitch, 0, Qt::AlignVCenter);

	setStatus(false);
	return header;
}

QWidget *DashboardDock::buildPresets()
{
	auto *section = new QWidget(this);
	auto *layout = new QVBoxLayout(section);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);
	layout->addWidget(sectionTitle(QStringLiteral("◎"), T("NVoice.Section.Style"), section));

	presetGrid_ = new QGridLayout();
	presetGrid_->setSpacing(8);
	layout->addLayout(presetGrid_);

	presetGroup_ = new QButtonGroup(section);
	presetGroup_->setExclusive(true);

	struct PresetDef {
		config::Profile profile;
		const char *glyph;
		const char *key;
	};
	const PresetDef presets[] = {
		{config::Profile::Chat, "💬", "NVoice.Preset.Chat"},
		{config::Profile::Game, "🎮", "NVoice.Preset.Game"},
		{config::Profile::Singing, "🎤", "NVoice.Preset.Singing"},
		{config::Profile::LateNight, "🌙", "NVoice.Preset.LateNight"},
	};

	for (const auto &def : presets) {
		auto *button = new QPushButton(QStringLiteral("%1  %2").arg(QString::fromUtf8(def.glyph), T(def.key)),
					       section);
		button->setObjectName(QStringLiteral("NVoicePreset"));
		button->setCheckable(true);
		button->setCursor(Qt::PointingHandCursor);
		button->setToolTip(T("NVoice.Preset.Tooltip"));
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		button->setChecked(def.profile == settings_.profile);
		presetGroup_->addButton(button, static_cast<int>(def.profile));
		presetButtons_.push_back(button);
	}
	layoutPresets(false);

	connect(presetGroup_, &QButtonGroup::idClicked, this, [this](int id) {
		settings_.profile = static_cast<config::Profile>(id);
		persist();
		applyPreset(settings_.profile);
	});

	calibButton_ = new QPushButton(QStringLiteral("🎙  ") + T("NVoice.Calib.Button"), section);
	calibButton_->setObjectName(QStringLiteral("NVoiceSecondary"));
	calibButton_->setCursor(Qt::PointingHandCursor);
	calibButton_->setToolTip(T("NVoice.Calib.Button.Tooltip"));
	connect(calibButton_, &QPushButton::clicked, this, &DashboardDock::openCalibration);
	layout->addWidget(calibButton_);

	presetNotice_ = new QFrame(section);
	presetNotice_->setObjectName(QStringLiteral("NVoiceNotice"));
	auto *noticeLayout = new QHBoxLayout(presetNotice_);
	noticeLayout->setContentsMargins(12, 6, 6, 6);
	presetNoticeText_ = new QLabel(presetNotice_);
	presetNoticeText_->setWordWrap(true);
	noticeLayout->addWidget(presetNoticeText_, 1);
	undoButton_ = new QPushButton(T("NVoice.Preset.Undo"), presetNotice_);
	undoButton_->setObjectName(QStringLiteral("NVoiceSecondary"));
	undoButton_->setCursor(Qt::PointingHandCursor);
	connect(undoButton_, &QPushButton::clicked, this, &DashboardDock::undoPreset);
	noticeLayout->addWidget(undoButton_);
	presetNotice_->setVisible(false);
	layout->addWidget(presetNotice_);
	return section;
}

QWidget *DashboardDock::buildMicCard()
{
	auto *section = new QWidget(this);
	auto *layout = new QVBoxLayout(section);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);

	auto *headerRow = new QHBoxLayout();
	headerRow->setSpacing(8);
	auto *micTitle = sectionTitle(QStringLiteral("🎙"), T("NVoice.Mic"), section);
	headerRow->addWidget(micTitle);

	sourceLabel_ = new QLabel(section);
	sourceLabel_->setProperty("muted", true);
	headerRow->addWidget(sourceLabel_);

	sourceCombo_ = new QComboBox(section);
	sourceCombo_->setObjectName(QStringLiteral("NVoiceSourceCombo"));
	sourceCombo_->setVisible(false);
	headerRow->addWidget(sourceCombo_);
	headerRow->addStretch(1);

	inputChip_ = new QLabel(section);
	inputChip_->setObjectName(QStringLiteral("NVoiceChip"));
	headerRow->addWidget(inputChip_);
	layout->addLayout(headerRow);

	connect(sourceCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (index < 0)
			return;
		selectedId_ = sourceCombo_->itemData(index).toULongLong();
		selected_.reset();
		meter_->clear();
		outputMeter_->clear();
		refreshSources();
	});

	auto *card = new QFrame(section);
	card->setObjectName(QStringLiteral("NVoiceCard"));
	auto *cardLayout = new QVBoxLayout(card);
	cardLayout->setContentsMargins(14, 12, 14, 12);
	cardLayout->setSpacing(8);

	meterArea_ = new QWidget(card);
	auto *meterLayout = new QVBoxLayout(meterArea_);
	meterLayout->setContentsMargins(0, 0, 0, 0);
	meterLayout->setSpacing(8);

	auto *levelRow = new QHBoxLayout();
	auto *levelLabel = new QLabel(T("NVoice.Input.Level"), meterArea_);
	levelValue_ = new QLabel(formatDb(-120.0f), meterArea_);
	levelValue_->setObjectName(QStringLiteral("NVoiceLevelValue"));
	levelRow->addWidget(levelLabel);
	levelRow->addStretch(1);
	levelRow->addWidget(levelValue_);
	meterLayout->addLayout(levelRow);

	meter_ = new LevelMeterWidget(meterArea_);
	meterLayout->addWidget(meter_);

	auto *peakRow = new QHBoxLayout();
	auto *peakLabel = new QLabel(QStringLiteral("PEAK"), meterArea_);
	peakLabel->setProperty("muted", true);
	peakValue_ = new QLabel(formatDb(-120.0f), meterArea_);
	peakValue_->setProperty("muted", true);
	peakRow->addWidget(peakLabel);
	peakRow->addStretch(1);
	peakRow->addWidget(peakValue_);
	meterLayout->addLayout(peakRow);

	auto *outputRow = new QHBoxLayout();
	auto *outputLabel = new QLabel(T("NVoice.Output.Level"), meterArea_);
	outputValue_ = new QLabel(formatDb(-120.0f), meterArea_);
	outputValue_->setObjectName(QStringLiteral("NVoiceOutputValue"));
	outputRow->addWidget(outputLabel);
	outputRow->addStretch(1);
	outputRow->addWidget(outputValue_);
	meterLayout->addSpacing(4);
	meterLayout->addLayout(outputRow);
	outputMeter_ = new LevelMeterWidget(meterArea_);
	meterLayout->addWidget(outputMeter_);

	dashboardWave_ = new WaveformWidget(waveHistory_.get(), meterArea_);
	meterLayout->addSpacing(4);
	meterLayout->addWidget(dashboardWave_);
	cardLayout->addWidget(meterArea_);

	noSourceHint_ = new QLabel(T("NVoice.Input.NoSource"), card);
	noSourceHint_->setObjectName(QStringLiteral("NVoiceHint"));
	noSourceHint_->setWordWrap(true);
	cardLayout->addWidget(noSourceHint_);

	layout->addWidget(card);
	return section;
}

QWidget *DashboardDock::buildFeatureList()
{
	auto *section = new QWidget(this);
	auto *layout = new QVBoxLayout(section);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);
	layout->addWidget(sectionTitle(QStringLiteral("☰"), T("NVoice.Section.Adjust"), section));

	// Features still marked `available = false` show as Coming Soon.
	const FeatureRow::Spec adjustSpecs[] = {
		{"noise_suppression", QStringLiteral("≋"), T("NVoice.Feature.NoiseSuppression.Title"),
		 T("NVoice.Feature.NoiseSuppression.Desc"), false, true},
		{"compressor", QStringLiteral("⇅"), T("NVoice.Feature.Compressor.Title"),
		 T("NVoice.Feature.Compressor.Desc"), true, true},
		{"auto_leveler", QStringLiteral("◢"), T("NVoice.Feature.AutoLeveler.Title"),
		 T("NVoice.Feature.AutoLeveler.Desc"), true, true},
		{"exciter", QStringLiteral("◆"), T("NVoice.Feature.Exciter.Title"), T("NVoice.Feature.Exciter.Desc"),
		 false, true},
		{"ducking", QStringLiteral("♪"), T("NVoice.Feature.Ducking.Title"), T("NVoice.Feature.Ducking.Desc"),
		 false, true},
	};

	auto *list = new QFrame(section);
	list->setObjectName(QStringLiteral("NVoiceFeatureList"));
	auto *listLayout = new QVBoxLayout(list);
	listLayout->setContentsMargins(0, 0, 0, 0);
	listLayout->setSpacing(0);
	for (const auto &spec : adjustSpecs) {
		auto *row = new FeatureRow(spec, list);
		listLayout->addWidget(row);
		rows_.push_back(row);
	}
	rows_.back()->setLast(true);
	layout->addWidget(list);

	// Balance check and waveform live in their own cards.
	const FeatureRow::Spec extraSpecs[] = {
		{"balance", QStringLiteral("▥"), T("NVoice.Feature.Balance.Title"), T("NVoice.Feature.Balance.Desc"),
		 false, false},
		{"waveform", QStringLiteral("∿"), T("NVoice.Feature.Waveform.Title"), T("NVoice.Feature.Waveform.Desc"),
		 true, true, false},
	};
	for (const auto &spec : extraSpecs) {
		auto *card = new QFrame(section);
		card->setObjectName(QStringLiteral("NVoiceFeatureList"));
		auto *cardLayout = new QVBoxLayout(card);
		cardLayout->setContentsMargins(0, 0, 0, 0);
		auto *row = new FeatureRow(spec, card);
		row->setLast(true);
		cardLayout->addWidget(row);
		rows_.push_back(row);
		layout->addWidget(card);
	}

	for (auto *row : rows_) {
		if (row->featureId() == QLatin1String("waveform"))
			row->setChecked(settings_.waveformVisible);
		connect(row, &FeatureRow::toggled, this, [this](const QString &id, bool on) {
			if (id == QLatin1String("waveform")) {
				settings_.waveformVisible = on;
				persist();
				setWaveformVisible(on);
			} else {
				onFeatureToggled(id, on);
			}
		});
		connect(row, &FeatureRow::openRequested, this, &DashboardDock::openPage);
	}
	return section;
}

void DashboardDock::layoutPresets(bool compact)
{
	presetsCompact_ = compact;
	for (auto *b : presetButtons_)
		presetGrid_->removeWidget(b);
	const int columns = compact ? 2 : 4;
	for (int i = 0; i < presetButtons_.size(); ++i)
		presetGrid_->addWidget(presetButtons_[i], i / columns, i % columns);
}

void DashboardDock::resizeEvent(QResizeEvent *event)
{
	QFrame::resizeEvent(event);
	const bool compact = event->size().width() < kCompactPresetWidth;
	if (compact != presetsCompact_)
		layoutPresets(compact);
}

// ---------------------------------------------------------------------------
// Theme & persistence
// ---------------------------------------------------------------------------

void DashboardDock::applyTheme(config::Theme theme)
{
	const Palette &p = paletteFor(theme);
	setStyleSheet(styleSheetFor(theme));
	meter_->setColors(p.meterTrack, p.meterFillStart, p.meterFillEnd, p.meterHot);
	outputMeter_->setColors(p.meterTrack, p.meterFillStart, p.meterFillEnd, p.meterHot);
	for (auto *page : {compressorPage_, levelerPage_})
		page->applyPalette(p.accent, p.toggleOff, p.meterTrack);
	calibrationPage_->applyPalette(p.accent, p.meterTrack, p.meterFillStart, p.meterFillEnd, p.meterHot);
	for (auto *wave : {dashboardWave_, compressorWave_})
		wave->setColors(p.border, p.textMuted, p.accent, p.meterHot, p.textMuted);
	for (auto *row : rows_) {
		if (row->toggle())
			row->toggle()->setColors(p.accent, p.toggleOff, QColor(Qt::white));
	}
}

void DashboardDock::persist()
{
	store_.save(settings_);
}

// ---------------------------------------------------------------------------
// Live data
// ---------------------------------------------------------------------------

void DashboardDock::setStatus(bool active)
{
	statusBadge_->setText(active ? T("NVoice.Status.Active") : T("NVoice.Status.Inactive"));
	statusBadge_->setToolTip(active ? QString() : T("NVoice.Status.Inactive.Tooltip"));
	statusBadge_->setProperty("state", active ? "active" : "inactive");
	repolish(statusBadge_);
}

void DashboardDock::showNoSource()
{
	stopCalibration();
	calibButton_->setEnabled(false);
	selectedId_ = 0;
	if (selected_)
		selected_->chain.engine.setWaveformEnabled(false);
	selected_.reset();
	selectedFilter_ = nullptr;
	selectedEnabled_ = false;
	sourceCombo_->setVisible(false);
	sourceLabel_->clear();
	meterArea_->setVisible(false);
	noSourceHint_->setVisible(true);
	inputChip_->setVisible(false);
	meter_->clear();
	outputMeter_->clear();
	presetNotice_->setVisible(false);
	for (auto *row : rows_)
		row->setInteractive(false);
	for (auto *page : {compressorPage_, levelerPage_})
		page->setControlsEnabled(false);
	if (stack_->currentIndex() != 0)
		showDashboard();
}

void DashboardDock::refreshSources()
{
	struct Item {
		std::uint64_t id;
		QString name;
		bool enabled;
		std::shared_ptr<obs::FilterShared> shared;
		OBSWeakSource weak;
	};
	std::vector<Item> items;

	// Resolve weak refs outside the registry lock. Releasing a strong ref here
	// can destroy a filter, which re-enters the registry (remove()).
	for (const auto &handle : obs::FilterRegistry::instance().snapshot()) {
		OBSSource filter = OBSGetStrongRef(handle.weakFilter);
		if (!filter)
			continue;
		obs_source_t *parent = obs_filter_get_parent(filter);
		const QString name = parent ? QString::fromUtf8(obs_source_get_name(parent)) : T("NVoice.Unassigned");
		items.push_back({handle.id, name, obs_source_enabled(filter), handle.shared, handle.weakFilter});
	}

	bool anyEnabled = false;
	for (const auto &item : items)
		anyEnabled = anyEnabled || item.enabled;
	setStatus(anyEnabled);

	if (items.empty()) {
		sourceSignature_.clear();
		showNoSource();
		return;
	}

	// Keep the current selection if it still exists; otherwise prefer the
	// first enabled instance.
	const Item *chosen = nullptr;
	for (const auto &item : items)
		if (item.id == selectedId_)
			chosen = &item;
	if (!chosen) {
		for (const auto &item : items)
			if (item.enabled) {
				chosen = &item;
				break;
			}
	}
	if (!chosen)
		chosen = &items.front();

	const bool selectionChanged = chosen->id != selectedId_ || !selected_;
	if (selectionChanged) {
		meter_->clear();
		outputMeter_->clear();
		stopCalibration();
		if (selected_)
			selected_->chain.engine.setWaveformEnabled(false);
		waveHistory_->clear();
	}
	selectedId_ = chosen->id;
	selected_ = chosen->shared;
	selectedFilter_ = chosen->weak;
	selectedEnabled_ = chosen->enabled;

	// Rebuild the picker only when the list actually changed.
	QString signature;
	for (const auto &item : items)
		signature += QString::number(item.id) + QLatin1Char(':') + item.name + QLatin1Char('\n');
	if (signature != sourceSignature_) {
		sourceSignature_ = signature;
		const QSignalBlocker blocker(sourceCombo_);
		sourceCombo_->clear();
		for (const auto &item : items)
			sourceCombo_->addItem(item.name, QVariant::fromValue<qulonglong>(item.id));
	}
	{
		const QSignalBlocker blocker(sourceCombo_);
		sourceCombo_->setCurrentIndex(sourceCombo_->findData(QVariant::fromValue<qulonglong>(selectedId_)));
	}

	const bool multiple = items.size() > 1;
	sourceCombo_->setVisible(multiple);
	sourceLabel_->setText(multiple ? QString() : QStringLiteral("(%1)").arg(chosen->name));
	meterArea_->setVisible(true);
	noSourceHint_->setVisible(false);
	inputChip_->setVisible(true);
	calibButton_->setEnabled(true);
	for (auto *row : rows_)
		row->setInteractive(true);
	for (auto *page : {compressorPage_, levelerPage_})
		page->setControlsEnabled(true);
	if (selectionChanged) {
		syncFromFilter();
		setWaveformVisible(settings_.waveformVisible);
	}
}

void DashboardDock::tick()
{
	if (++tickCount_ >= kSourceRefreshTicks) {
		tickCount_ = 0;
		refreshSources();
	}
	if (!selected_)
		return;

	auto &chain = selected_->chain;
	const auto reading = chain.engine.inputMeter().read();
	meter_->setLevels(reading.rmsDb, reading.peakDb);
	levelValue_->setText(formatDb(reading.rmsDb));
	peakValue_->setText(formatDb(meter_->heldPeakDb()));

	const auto output = chain.engine.outputMeter().read();
	outputMeter_->setLevels(output.rmsDb, output.peakDb);
	outputValue_->setText(formatDb(output.rmsDb));

	pollWaveform();

	if (calibrating_) {
		engine::FrameStats frames[64];
		std::size_t n;
		while ((n = chain.engine.analysis().read(frames, 64)) > 0)
			calibFrames_.insert(calibFrames_.end(), frames, frames + n);
		const double elapsed = static_cast<double>(calibClock_.elapsed()) / 1000.0;
		calibrationPage_->setLevel(reading.rmsDb, reading.peakDb);
		calibrationPage_->setProgress(elapsed);
		if (elapsed >= CalibrationPage::kDurationSec)
			finishCalibration();
	}

	// Only the visible page reads its meter (reading resets the peak hold).
	if (stack_->currentWidget()->isAncestorOf(compressorPage_))
		compressorPage_->setMeter(chain.compressor.enabled() ? chain.compressor.readGainReductionDb() : 0.0f);
	else if (stack_->currentWidget()->isAncestorOf(levelerPage_))
		levelerPage_->setMeter(chain.leveler.enabled() ? chain.leveler.currentGainDb() : 0.0f);

	const qint64 now = clock_.elapsed();
	if (reading.rmsDb > kSignalThresholdDb)
		lastSignalMs_ = now;
	const bool hasSignal = lastSignalMs_ >= 0 && now - lastSignalMs_ < kSignalHoldMs;

	const char *state = !selectedEnabled_ ? "off" : (hasSignal ? "ok" : "idle");
	const char *key = !selectedEnabled_ ? "NVoice.Input.FilterOff"
					    : (hasSignal ? "NVoice.Input.Ok" : "NVoice.Input.Idle");
	if (inputChip_->property("state").toByteArray() != state) {
		inputChip_->setText(QStringLiteral("● ") + T(key));
		inputChip_->setProperty("state", state);
		repolish(inputChip_);
	}
}

// ---------------------------------------------------------------------------
// Filter settings
// ---------------------------------------------------------------------------

bool DashboardDock::writeFilter(const std::function<void(obs_data_t *)> &fill)
{
	OBSSource filter = OBSGetStrongRef(selectedFilter_);
	if (!filter)
		return false;
	OBSDataAutoRelease changes = obs_data_create();
	fill(changes);
	obs_source_update(filter, changes); // merges and calls the filter's update()
	return true;
}

void DashboardDock::syncFromFilter()
{
	OBSSource filter = OBSGetStrongRef(selectedFilter_);
	if (!filter)
		return;
	OBSDataAutoRelease settings = obs_source_get_settings(filter);

	const bool compOn = obs_data_get_bool(settings, obs::kCompressorEnabled);
	const bool levOn = obs_data_get_bool(settings, obs::kLevelerEnabled);
	for (auto *row : rows_) {
		if (row->featureId() == QLatin1String("compressor"))
			row->setChecked(compOn);
		else if (row->featureId() == QLatin1String("auto_leveler"))
			row->setChecked(levOn);
	}
	compressorPage_->setEnabledState(compOn);
	levelerPage_->setEnabledState(levOn);

	for (auto *page : {compressorPage_, levelerPage_}) {
		for (auto *control : page->controls())
			control->setValue(obs::getParam(settings, control->param()));
	}
}

void DashboardDock::onFeatureToggled(const QString &featureId, bool enabled)
{
	const char *key = featureId == QLatin1String("compressor") ? obs::kCompressorEnabled : obs::kLevelerEnabled;
	writeFilter([&](obs_data_t *d) { obs_data_set_bool(d, key, enabled); });
	syncFromFilter(); // keep the row and the page in step
}

void DashboardDock::onParamChanged(const QString &paramId, double value)
{
	const auto id = paramId.toStdString();
	const auto *spec = engine::findParameter(id);
	if (!spec)
		return;

	writeFilter([&](obs_data_t *d) {
		obs::setParam(d, *spec, value);
		// The beginner "strength" drives threshold and ratio.
		if (spec == &engine::params::kCompStrength) {
			const auto tr = engine::compressorFromStrength(static_cast<float>(value));
			obs::setParam(d, engine::params::kCompThreshold, tr.threshold);
			obs::setParam(d, engine::params::kCompRatio, tr.ratio);
		}
	});

	if (spec == &engine::params::kCompStrength) {
		const auto tr = engine::compressorFromStrength(static_cast<float>(value));
		compressorPage_->setValue(QString::fromUtf8(engine::params::kCompThreshold.id.data()), tr.threshold);
		compressorPage_->setValue(QString::fromUtf8(engine::params::kCompRatio.id.data()), tr.ratio);
	}
}

void DashboardDock::resetFeature(const QString &featureId)
{
	writeFilter([&](obs_data_t *d) {
		if (featureId == QLatin1String("compressor")) {
			for (const auto *spec : obs::kCompressorParams)
				obs::setParam(d, *spec, spec->defaultValue);
		} else {
			for (const auto *spec : obs::kLevelerParams)
				obs::setParam(d, *spec, spec->defaultValue);
		}
	});
	syncFromFilter();
}

void DashboardDock::applyPreset(config::Profile profile)
{
	OBSSource filter = OBSGetStrongRef(selectedFilter_);
	if (!filter)
		return; // selection is saved; nothing to apply yet

	rememberForUndo(); // so the preset can be undone (§4.9)

	writeFilter([&](obs_data_t *d) { writePresetValues(d, profile); });
	syncFromFilter();

	const QString name =
		presetButtons_.value(static_cast<int>(profile))
			? presetButtons_[static_cast<int>(profile)]->text().section(QStringLiteral("  "), 1)
			: QString();
	showAppliedNotice(T("NVoice.Preset.Applied").arg(name));
}

void DashboardDock::writePresetValues(obs_data_t *d, config::Profile profile)
{
	namespace p = engine::params;
	const auto &v = config::presetFor(profile);
	const auto tr = engine::compressorFromStrength(v.compStrength);
	obs_data_set_bool(d, obs::kCompressorEnabled, true);
	obs_data_set_bool(d, obs::kLevelerEnabled, true);
	obs::setParam(d, p::kCompStrength, v.compStrength);
	obs::setParam(d, p::kCompThreshold, tr.threshold);
	obs::setParam(d, p::kCompRatio, tr.ratio);
	obs::setParam(d, p::kCompAttack, v.compAttackMs);
	obs::setParam(d, p::kCompRelease, v.compReleaseMs);
	obs::setParam(d, p::kCompMakeup, v.compMakeupDb);
	obs::setParam(d, p::kLevelerTarget, v.levelerTargetDb);
	obs::setParam(d, p::kLevelerMaxGain, v.levelerMaxGainDb);
	obs::setParam(d, p::kLevelerMaxCut, v.levelerMaxCutDb);
	obs::setParam(d, p::kLevelerSpeed, v.levelerSpeedSec);
	obs::setParam(d, p::kLevelerGate, v.levelerGateDb);
}

void DashboardDock::rememberForUndo()
{
	OBSSource filter = OBSGetStrongRef(selectedFilter_);
	if (!filter)
		return;
	OBSDataAutoRelease current = obs_source_get_settings(filter);
	OBSDataAutoRelease snapshot = obs_data_create();
	obs_data_set_bool(snapshot, obs::kCompressorEnabled, obs_data_get_bool(current, obs::kCompressorEnabled));
	obs_data_set_bool(snapshot, obs::kLevelerEnabled, obs_data_get_bool(current, obs::kLevelerEnabled));
	for (const auto *spec : engine::params::kAll)
		obs::setParam(snapshot, *spec, obs::getParam(current, *spec));
	undoJson_ = obs_data_get_json(snapshot);
	undoFilterId_ = selectedId_;
}

void DashboardDock::showAppliedNotice(const QString &text)
{
	presetNoticeText_->setText(text);
	undoButton_->setVisible(true);
	presetNotice_->setVisible(true);
}

void DashboardDock::undoPreset()
{
	if (undoJson_.empty() || undoFilterId_ != selectedId_) {
		presetNotice_->setVisible(false);
		return;
	}
	const std::string json = undoJson_;
	writeFilter([&](obs_data_t *d) {
		OBSDataAutoRelease snapshot = obs_data_create_from_json(json.c_str());
		obs_data_apply(d, snapshot);
	});
	undoJson_.clear();
	syncFromFilter();
	presetNoticeText_->setText(T("NVoice.Preset.Undone"));
	undoButton_->setVisible(false);
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void DashboardDock::openPage(const QString &featureId)
{
	if (!selected_)
		return;
	syncFromFilter();
	FeaturePage *page = featureId == QLatin1String("compressor") ? compressorPage_ : levelerPage_;
	if (featureId == QLatin1String("compressor"))
		selected_->chain.compressor.readGainReductionDb(); // start the meter fresh
	stack_->setCurrentWidget(page->parentWidget());
}

void DashboardDock::showDashboard()
{
	stack_->setCurrentIndex(0);
	syncFromFilter();
}

// ---------------------------------------------------------------------------
// Waveform
// ---------------------------------------------------------------------------

void DashboardDock::setWaveformVisible(bool visible)
{
	for (auto *row : rows_) {
		if (row->featureId() == QLatin1String("waveform"))
			row->setChecked(visible);
	}
	dashboardWave_->setVisible(visible);
	compressorWave_->parentWidget()->setVisible(visible);

	if (!selected_)
		return;
	auto &engine = selected_->chain.engine;
	if (visible && !engine.waveformEnabled()) {
		// Drop buckets left from an earlier session, then start capturing.
		engine::WaveBucket discard[256];
		while (engine.inputWaveform().read(discard, 256) > 0) {
		}
		while (engine.outputWaveform().read(discard, 256) > 0) {
		}
		waveHistory_->clear();
	}
	engine.setWaveformEnabled(visible);
}

void DashboardDock::pollWaveform()
{
	if (!selected_ || !settings_.waveformVisible)
		return;
	auto &chain = selected_->chain;

	engine::WaveBucket in[64];
	engine::WaveBucket out[64];
	std::size_t total = 0;
	for (;;) {
		const std::size_t nIn = chain.engine.inputWaveform().read(in, 64);
		const std::size_t nOut = chain.engine.outputWaveform().read(out, nIn);
		const std::size_t n = std::min(nIn, nOut); // equal: both are pushed in the same callback
		waveHistory_->append(in, out, n);
		total += n;
		if (nIn < 64)
			break;
	}

	// The compressor sees the input after the auto leveler, so its threshold
	// relative to the input moves with the leveler's current gain.
	std::optional<float> threshold;
	if (chain.compressor.enabled()) {
		const float lift = chain.leveler.enabled() ? chain.leveler.currentGainDb() : 0.0f;
		threshold = chain.compressor.settings().thresholdDb - lift;
	}
	compressorWave_->setThresholdDb(threshold);

	if (total > 0) {
		if (dashboardWave_->isVisible())
			dashboardWave_->update();
		if (compressorWave_->isVisible())
			compressorWave_->update();
	}
}

// ---------------------------------------------------------------------------
// Auto setup (声に合わせて自動設定)
// ---------------------------------------------------------------------------

void DashboardDock::openCalibration()
{
	if (!selected_)
		return;
	const int index = static_cast<int>(settings_.profile);
	const QString name = presetButtons_.value(index)
				     ? presetButtons_[index]->text().section(QStringLiteral("  "), 1)
				     : QString();
	calibrationPage_->setProfile(settings_.profile, name);
	calibrationPage_->showIntro();
	stack_->setCurrentWidget(calibrationPage_->parentWidget());
}

void DashboardDock::startCalibration()
{
	if (!selected_)
		return;
	auto &engine = selected_->chain.engine;
	engine.setAnalysisEnabled(false);
	engine::FrameStats discard[64];
	while (engine.analysis().read(discard, 64) > 0) {
	}
	calibFrames_.clear();
	engine.setAnalysisEnabled(true);
	calibClock_.start();
	calibrating_ = true;
	calibrationPage_->showRecording();
}

void DashboardDock::stopCalibration()
{
	if (selected_)
		selected_->chain.engine.setAnalysisEnabled(false);
	if (calibrating_) {
		calibrating_ = false;
		calibrationPage_->showIntro();
	}
}

void DashboardDock::finishCalibration()
{
	calibrating_ = false;
	if (selected_)
		selected_->chain.engine.setAnalysisEnabled(false);

	OBSSource filter = OBSGetStrongRef(selectedFilter_);
	if (!filter) {
		calibrationPage_->showIntro();
		return;
	}
	OBSDataAutoRelease settings = obs_source_get_settings(filter);
	namespace p = engine::params;
	const CalibrationPage::Current current{
		obs::getParam(settings, p::kLevelerTarget),  obs::getParam(settings, p::kLevelerGate),
		obs::getParam(settings, p::kLevelerMaxGain), obs::getParam(settings, p::kLevelerMaxCut),
		obs::getParam(settings, p::kCompStrength),
	};
	// Start from the selected stream style, as the page says.
	const auto &base = config::presetFor(settings_.profile);
	calibResult_ = engine::analyzeVoice(calibFrames_, base.levelerTargetDb, base.compStrength);
	calibrationPage_->showResult(calibResult_, current);
}

void DashboardDock::applyCalibration()
{
	if (!calibResult_.usable)
		return;
	rememberForUndo();

	namespace p = engine::params;
	const auto &prop = calibResult_.proposal;
	const auto tr = engine::compressorFromStrength(prop.compStrength);
	writeFilter([&](obs_data_t *d) {
		writePresetValues(d, settings_.profile);
		obs::setParam(d, p::kLevelerTarget, prop.targetDb);
		obs::setParam(d, p::kLevelerGate, prop.gateDb);
		obs::setParam(d, p::kLevelerMaxGain, prop.maxGainDb);
		obs::setParam(d, p::kLevelerMaxCut, prop.maxCutDb);
		obs::setParam(d, p::kCompStrength, prop.compStrength);
		obs::setParam(d, p::kCompThreshold, tr.threshold);
		obs::setParam(d, p::kCompRatio, tr.ratio);
	});
	showDashboard();
	showAppliedNotice(T("NVoice.Calib.Applied"));
}

} // namespace nvoice::ui
