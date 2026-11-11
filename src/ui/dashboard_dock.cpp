#include "dashboard_dock.hpp"

#include "feature_row.hpp"
#include "level_meter_widget.hpp"
#include "theme.hpp"
#include "toggle_switch.hpp"
#include "../obs/filter_registry.hpp"

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
#include <QStyle>
#include <QVBoxLayout>

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
	buildUi();
	applyTheme(settings_.theme);

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
	selected_.reset();
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

	auto *content = new QWidget(scroll);
	content->setObjectName(QStringLiteral("NVoiceContent"));
	content->setMinimumWidth(320); // stays operable in narrow docks
	scroll->setWidget(content);

	auto *layout = new QVBoxLayout(content);
	layout->setContentsMargins(16, 14, 16, 16);
	layout->setSpacing(14);

	layout->addWidget(buildHeader());
	layout->addWidget(buildPresets());
	layout->addWidget(buildMicCard());
	layout->addWidget(buildFeatureList());
	layout->addStretch(1);
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
	});
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

	// Phase 1: every DSP feature is still Coming Soon. Flip `available` to
	// true as each feature lands (compressor / auto leveler in Phase 2, ...).
	const FeatureRow::Spec adjustSpecs[] = {
		{"noise_suppression", QStringLiteral("≋"), T("NVoice.Feature.NoiseSuppression.Title"),
		 T("NVoice.Feature.NoiseSuppression.Desc"), false, true},
		{"compressor", QStringLiteral("⇅"), T("NVoice.Feature.Compressor.Title"),
		 T("NVoice.Feature.Compressor.Desc"), false, true},
		{"auto_leveler", QStringLiteral("◢"), T("NVoice.Feature.AutoLeveler.Title"),
		 T("NVoice.Feature.AutoLeveler.Desc"), false, true},
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
		 false, true},
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
			}
		});
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
	selectedId_ = 0;
	selected_.reset();
	selectedEnabled_ = false;
	sourceCombo_->setVisible(false);
	sourceLabel_->clear();
	meterArea_->setVisible(false);
	noSourceHint_->setVisible(true);
	inputChip_->setVisible(false);
	meter_->clear();
}

void DashboardDock::refreshSources()
{
	struct Item {
		std::uint64_t id;
		QString name;
		bool enabled;
		std::shared_ptr<obs::FilterShared> shared;
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
		items.push_back({handle.id, name, obs_source_enabled(filter), handle.shared});
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

	if (chosen->id != selectedId_)
		meter_->clear();
	selectedId_ = chosen->id;
	selected_ = chosen->shared;
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
}

void DashboardDock::tick()
{
	if (++tickCount_ >= kSourceRefreshTicks) {
		tickCount_ = 0;
		refreshSources();
	}
	if (!selected_)
		return;

	const auto reading = selected_->engine.inputMeter().read();
	meter_->setLevels(reading.rmsDb, reading.peakDb);
	levelValue_->setText(formatDb(reading.rmsDb));
	peakValue_->setText(formatDb(meter_->heldPeakDb()));

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

} // namespace nvoice::ui
