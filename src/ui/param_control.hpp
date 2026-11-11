// N Voice for Studio - UI
// One parameter: label, slider and number input that stay in sync
// (requirements §5.4). Range, default and unit come from the parameter table.
#pragma once

#include <nvoice/engine/parameters.hpp>

#include <QString>
#include <QWidget>

class QDoubleSpinBox;
class QSlider;

namespace nvoice::ui {

class ParamControl : public QWidget {
	Q_OBJECT

public:
	struct Spec {
		const engine::ParameterSpec *param;
		QString label; // intent-focused, e.g. "どのくらい大きな声から抑える？"
		QString help;  // tooltip; may name the technical term (Threshold)
		int decimals;
		double step;
	};

	explicit ParamControl(const Spec &spec, QWidget *parent = nullptr);

	const engine::ParameterSpec &param() const { return *spec_.param; }
	double value() const;

	// Updates the display without emitting valueChanged.
	void setValue(double value);

signals:
	// User changed the value (dragging the slider or confirming the number).
	void valueChanged(const QString &id, double value);

private:
	int toSlider(double value) const;
	double fromSlider(int position) const;

	Spec spec_;
	QSlider *slider_ = nullptr;
	QDoubleSpinBox *spin_ = nullptr;
};

} // namespace nvoice::ui
