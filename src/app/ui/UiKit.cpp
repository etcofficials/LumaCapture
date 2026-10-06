#include "ui/UiKit.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <QVariant>

namespace luma::app::ui {

QFrame* panel(QWidget* parent, QVBoxLayout*& body, const QString& titleText, int margin)
{
    auto* f = new QFrame(parent);
    f->setObjectName("Panel");
    body = new QVBoxLayout(f);
    body->setContentsMargins(margin, margin - 2, margin, margin);
    body->setSpacing(8);
    if (!titleText.isEmpty())
        body->addWidget(title(titleText, f));
    return f;
}

QLabel* title(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName("PanelTitle");
    return l;
}

QLabel* hint(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName("Hint");
    l->setWordWrap(true);
    return l;
}

QLabel* dim(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName("Dim");
    return l;
}

QWidget* row(const QString& label, QWidget* field, QWidget* parent, const QString& tip)
{
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    auto* l = dim(label, w);
    l->setMinimumWidth(96);
    l->setBuddy(field);
    if (!tip.isEmpty()) {
        l->setToolTip(tip);
        field->setToolTip(tip);
    }
    field->setParent(w);
    if (field->accessibleName().isEmpty())
        field->setAccessibleName(label);
    h->addWidget(l);
    h->addWidget(field, 1);
    return w;
}

void SliderRow::setValue(int v) const
{
    const QSignalBlocker block(slider);
    slider->setValue(v);
    if (format)
        value->setText(format(slider->value()));
}

SliderRow sliderRow(const QString& label, int min, int max, QWidget* parent, std::function<QString(int)> format,
                    const QString& tip)
{
    SliderRow r;
    r.widget = new QWidget(parent);
    auto* h = new QHBoxLayout(r.widget);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    auto* l = dim(label, r.widget);
    l->setMinimumWidth(96);
    r.slider = new QSlider(Qt::Horizontal, r.widget);
    r.slider->setRange(min, max);
    r.slider->setAccessibleName(label);
    l->setBuddy(r.slider);
    r.value = new QLabel(r.widget);
    r.value->setMinimumWidth(44);
    r.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    r.format = std::move(format);
    if (!tip.isEmpty()) {
        l->setToolTip(tip);
        r.slider->setToolTip(tip);
    }
    h->addWidget(l);
    h->addWidget(r.slider, 1);
    h->addWidget(r.value);
    QLabel* valueLabel = r.value;
    const auto fmt = r.format;
    QObject::connect(r.slider, &QSlider::valueChanged, valueLabel, [valueLabel, fmt](int v) {
        if (fmt)
            valueLabel->setText(fmt(v));
    });
    return r;
}

bool selectData(QComboBox* combo, const QVariant& data)
{
    const int i = combo->findData(data);
    if (i < 0)
        return false;
    const QSignalBlocker block(combo);
    combo->setCurrentIndex(i);
    return true;
}

} // namespace luma::app::ui
