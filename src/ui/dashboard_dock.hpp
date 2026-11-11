// N Voice for Studio - UI
// Main dashboard shown as an OBS dock (requirements §5.3), plus the feature
// detail pages it navigates to.
#pragma once

#include "../config/app_settings.hpp"

#include <obs.hpp>

#include <QElapsedTimer>
#include <QFrame>
#include <QTimer>
#include <QVector>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class QButtonGroup;
class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace nvoice::obs {
struct FilterShared;
}

namespace nvoice::ui {

class FeaturePage;
class FeatureRow;
class LevelMeterWidget;

class DashboardDock : public QFrame {
	Q_OBJECT

public:
	explicit DashboardDock(QWidget *parent = nullptr);
	~DashboardDock() override;

	// Stops polling. Called when OBS starts shutting down.
	void shutdown();

	QSize sizeHint() const override;

protected:
	void resizeEvent(QResizeEvent *event) override;

private:
	void buildUi();
	QWidget *buildDashboard();
	QWidget *buildHeader();
	QWidget *buildPresets();
	QWidget *buildMicCard();
	QWidget *buildFeatureList();
	void buildPages();

	void applyTheme(config::Theme theme);
	void layoutPresets(bool compact);
	void persist();

	void tick();           // 30 fps meter refresh
	void refreshSources(); // list of sources with the N Voice filter
	void setStatus(bool active);
	void showNoSource();

	// Selected filter's settings
	bool writeFilter(const std::function<void(obs_data_t *)> &fill);
	void syncFromFilter(); // filter settings -> rows and pages
	void onFeatureToggled(const QString &featureId, bool enabled);
	void onParamChanged(const QString &paramId, double value);
	void resetFeature(const QString &featureId);
	void applyPreset(config::Profile profile);
	void undoPreset();
	void openPage(const QString &featureId);
	void showDashboard();

	config::SettingsStore store_;
	config::AppSettings settings_;

	QTimer timer_;
	int tickCount_ = 0;

	QStackedWidget *stack_ = nullptr;
	FeaturePage *compressorPage_ = nullptr;
	FeaturePage *levelerPage_ = nullptr;

	// Header
	QLabel *statusBadge_ = nullptr;
	QPushButton *lightButton_ = nullptr;
	QPushButton *darkButton_ = nullptr;

	// Presets
	QGridLayout *presetGrid_ = nullptr;
	QVector<QPushButton *> presetButtons_;
	QButtonGroup *presetGroup_ = nullptr;
	bool presetsCompact_ = false;
	QFrame *presetNotice_ = nullptr;
	QLabel *presetNoticeText_ = nullptr;
	QPushButton *undoButton_ = nullptr;
	std::string undoJson_; // filter settings before the last preset
	std::uint64_t undoFilterId_ = 0;

	// Mic card
	QComboBox *sourceCombo_ = nullptr;
	QLabel *sourceLabel_ = nullptr;
	QLabel *inputChip_ = nullptr;
	QWidget *meterArea_ = nullptr;
	QLabel *noSourceHint_ = nullptr;
	QLabel *levelValue_ = nullptr;
	QLabel *peakValue_ = nullptr;
	LevelMeterWidget *meter_ = nullptr;
	QLabel *outputValue_ = nullptr;
	LevelMeterWidget *outputMeter_ = nullptr;

	// Features
	QVector<FeatureRow *> rows_;

	// Currently displayed filter instance
	std::uint64_t selectedId_ = 0;
	std::shared_ptr<obs::FilterShared> selected_;
	OBSWeakSource selectedFilter_;
	bool selectedEnabled_ = false;
	QString sourceSignature_;
	QElapsedTimer clock_;
	qint64 lastSignalMs_ = -1;
};

} // namespace nvoice::ui
