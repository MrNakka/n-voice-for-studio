// N Voice for Studio - UI
// One row of the "音声を調整する" list: icon, title, subtitle, and either an
// ON/OFF switch or a "Coming Soon" badge (requirements v0.2 §5.3).
#pragma once

#include <QFrame>
#include <QString>

class QLabel;

namespace nvoice::ui {

class ToggleSwitch;

class FeatureRow : public QFrame {
	Q_OBJECT

public:
	struct Spec {
		QString id;
		QString icon; // short glyph
		QString title;
		QString subtitle;
		bool available = false; // false -> Coming Soon
		bool hasToggle = true;  // false -> navigation-only row
	};

	explicit FeatureRow(const Spec &spec, QWidget *parent = nullptr);

	const QString &featureId() const { return spec_.id; }
	bool isAvailable() const { return spec_.available; }

	void setSubtitle(const QString &text);
	void setChecked(bool checked);
	bool isChecked() const;
	void setLast(bool last);

	ToggleSwitch *toggle() const { return toggle_; }

signals:
	void toggled(const QString &featureId, bool enabled);
	void openRequested(const QString &featureId);

protected:
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	Spec spec_;
	QLabel *subtitle_ = nullptr;
	ToggleSwitch *toggle_ = nullptr;
};

} // namespace nvoice::ui
