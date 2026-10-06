#pragma once

#include <QString>

#include <functional>

class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QSlider;
class QVariant;
class QVBoxLayout;
class QWidget;

namespace luma::app::ui {

// Bordered panel; `body` receives the content layout.
QFrame* panel(QWidget* parent, QVBoxLayout*& body, const QString& title = {}, int margin = 12);
QLabel* title(const QString& text, QWidget* parent);
QLabel* hint(const QString& text, QWidget* parent);
QLabel* dim(const QString& text, QWidget* parent);

// "Label            [field]" on one line (field stretches). The label gets the tooltip too.
QWidget* row(const QString& label, QWidget* field, QWidget* parent, const QString& tip = {});

// Slider with its current value shown on the right.
struct SliderRow {
    QWidget* widget = nullptr;
    QSlider* slider = nullptr;
    QLabel* value = nullptr;
    std::function<QString(int)> format;
    void setValue(int v) const; // updates slider + label without emitting valueChanged
};
SliderRow sliderRow(const QString& label, int min, int max, QWidget* parent, std::function<QString(int)> format,
                    const QString& tip = {});

// Selects the combo entry whose itemData equals `data` (no signal); returns false if absent.
bool selectData(QComboBox* combo, const QVariant& data);

// Set while widgets are refreshed from the settings, so their change handlers do not
// write the same values back.
class Updating {
public:
    explicit Updating(bool& flag) : m_flag(flag), m_old(flag) { m_flag = true; }
    ~Updating() { m_flag = m_old; }
    Updating(const Updating&) = delete;
    Updating& operator=(const Updating&) = delete;

private:
    bool& m_flag;
    bool m_old;
};

} // namespace luma::app::ui
