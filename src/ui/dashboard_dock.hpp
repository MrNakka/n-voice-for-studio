// N Voice for Studio - UI
// Main dashboard shown as an OBS dock (requirements v0.2 §5.3).
#pragma once

#include "../config/app_settings.hpp"

#include <QFrame>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>

#include <cstdint>
#include <memory>

class QButtonGroup;
class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;

namespace nvoice::obs {
struct FilterShared;
}

namespace nvoice::ui {

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
	QWidget *buildHeader();
	QWidget *buildPresets();
	QWidget *buildMicCard();
	QWidget *buildFeatureList();

	void applyTheme(config::Theme theme);
	void layoutPresets(bool compact);
	void persist();

	void tick();           // 30 fps meter refresh
	void refreshSources(); // list of sources with the N Voice filter
	void setStatus(bool active);
	void showNoSource();

	config::SettingsStore store_;
	config::AppSettings settings_;

	QTimer timer_;
	int tickCount_ = 0;

	// Header
	QLabel *statusBadge_ = nullptr;
	QPushButton *lightButton_ = nullptr;
	QPushButton *darkButton_ = nullptr;

	// Presets
	QGridLayout *presetGrid_ = nullptr;
	QVector<QPushButton *> presetButtons_;
	QButtonGroup *presetGroup_ = nullptr;
	bool presetsCompact_ = false;

	// Mic card
	QComboBox *sourceCombo_ = nullptr;
	QLabel *sourceLabel_ = nullptr;
	QLabel *inputChip_ = nullptr;
	QWidget *meterArea_ = nullptr;
	QLabel *noSourceHint_ = nullptr;
	QLabel *levelValue_ = nullptr;
	QLabel *peakValue_ = nullptr;
	LevelMeterWidget *meter_ = nullptr;

	// Features
	QVector<FeatureRow *> rows_;

	// Currently displayed filter instance
	std::uint64_t selectedId_ = 0;
	std::shared_ptr<obs::FilterShared> selected_;
	bool selectedEnabled_ = false;
	QString sourceSignature_;
	QElapsedTimer clock_;
	qint64 lastSignalMs_ = -1;
};

} // namespace nvoice::ui
