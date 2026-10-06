#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

#include <utility>

namespace luma::app {
namespace {

Palette g_palette;

Palette makePalette(Theme t)
{
    Palette p;
    if (t == Theme::Light) {
        p.window = QColor(0xF2, 0xF4, 0xF7);
        p.surface = QColor(0xFF, 0xFF, 0xFF);
        p.surfaceAlt = QColor(0xEE, 0xF1, 0xF5);
        p.raised = QColor(0xE6, 0xEA, 0xF0);
        p.border = QColor(0xD5, 0xDA, 0xE2);
        p.text = QColor(0x1B, 0x22, 0x30);
        p.textDim = QColor(0x5B, 0x65, 0x75);
        p.accent = QColor(0x25, 0x63, 0xEB);
        p.selected = QColor(0xDC, 0xE7, 0xFD);
    } else {
        p.window = QColor(0x0F, 0x12, 0x18);
        p.surface = QColor(0x16, 0x1A, 0x21);
        p.surfaceAlt = QColor(0x1D, 0x22, 0x2B);
        p.raised = QColor(0x25, 0x2B, 0x36);
        p.border = QColor(0x2A, 0x30, 0x3B);
        p.text = QColor(0xE6, 0xE9, 0xEF);
        p.textDim = QColor(0x9A, 0xA3, 0xB2);
        p.accent = QColor(0x3D, 0x8B, 0xFF);
        p.selected = QColor(0x1E, 0x33, 0x58);
    }
    p.record = QColor(0xE5, 0x48, 0x4D);
    p.warning = QColor(0xE8, 0xA3, 0x2A);
    p.success = QColor(0x2F, 0xAE, 0x6B);
    return p;
}

} // namespace

const Palette& currentPalette() { return g_palette; }

void applyTheme(QApplication& app, Theme theme)
{
    g_palette = makePalette(theme);
    const Palette& p = g_palette;
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette pal;
    pal.setColor(QPalette::Window, p.window);
    pal.setColor(QPalette::WindowText, p.text);
    pal.setColor(QPalette::Base, p.surfaceAlt);
    pal.setColor(QPalette::AlternateBase, p.surface);
    pal.setColor(QPalette::Text, p.text);
    pal.setColor(QPalette::Button, p.surfaceAlt);
    pal.setColor(QPalette::ButtonText, p.text);
    pal.setColor(QPalette::Highlight, p.accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, p.surface);
    pal.setColor(QPalette::ToolTipText, p.text);
    pal.setColor(QPalette::PlaceholderText, p.textDim);
    pal.setColor(QPalette::Link, p.accent);
    pal.setColor(QPalette::Disabled, QPalette::Text, p.textDim);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, p.textDim);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, p.textDim);
    app.setPalette(pal);

    QString qss = QStringLiteral(R"(
* { font-family: "Segoe UI"; font-size: 9pt; }
QMainWindow, QDialog { background: @window; }
QToolTip { background: @surface; color: @text; border: 1px solid @border; padding: 5px 7px; border-radius: 4px; }

/* Top bar and navigation */
QFrame#TopBar { background: @window; border-bottom: 1px solid @border; }
QLabel#Brand { color: @text; font-size: 12pt; font-weight: 600; }
QLabel#BrandSub { color: @dim; font-size: 8pt; }
QPushButton#Nav { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 0 16px;
                  min-height: 34px; color: @dim; font-size: 10pt; }
QPushButton#Nav:hover { color: @text; background: @alt; }
QPushButton#Nav:checked { color: @text; background: @selected; border-color: @accent; }
QPushButton#Nav:focus { border-color: @accent; }

/* Panels and typography */
QFrame#Panel { background: @surface; border: 1px solid @border; border-radius: 8px; }
QFrame#Inset { background: @alt; border: 1px solid @border; border-radius: 6px; }
QLabel#PanelTitle { color: @text; font-size: 10pt; font-weight: 600; }
QLabel#PageTitle { color: @text; font-size: 13pt; font-weight: 600; }
QLabel#Dim { color: @dim; }
QLabel#Hint { color: @dim; font-size: 8pt; }
QLabel#Warn { color: @warn; }
QLabel#Ok { color: @success; }
QLabel#Timer { color: @text; font-size: 20pt; font-weight: 600; }
QLabel#StateText { color: @dim; font-size: 9.5pt; }
QLabel#Value { color: @text; font-weight: 600; }
QLabel#Pill { border-radius: 9px; padding: 1px 9px; font-size: 8pt; font-weight: 700; background: @alt; color: @dim; }
QLabel#Pill[state="rec"] { background: @record; color: white; }
QLabel#Pill[state="paused"] { background: @warn; color: #1a1300; }
QLabel#Pill[state="busy"] { background: @accent; color: white; }
QLabel#Pill[state="error"] { background: @record; color: white; }
QToolButton#SectionHeader { color: @text; font-size: 9.5pt; font-weight: 600; border: none; padding: 4px 2px;
                            text-align: left; background: transparent; }
QToolButton#SectionHeader:hover { color: @accent; }

/* Buttons */
QPushButton { background: @alt; border: 1px solid @border; border-radius: 6px; padding: 0 12px; min-height: 28px; color: @text; }
QPushButton:hover { border-color: @dimborder; background: @raised; }
QPushButton:pressed { background: @border; }
QPushButton:focus { border: 1px solid @accent; }
QPushButton:disabled { color: @dim; background: @surface; border-color: @border; }
QPushButton:checked { background: @selected; border-color: @accent; color: @text; }
QPushButton#Primary { background: @accent; border: 1px solid @accent; color: white; font-weight: 600; }
QPushButton#Primary:hover { background: @accenthover; }
QPushButton#Flat { background: transparent; border: none; color: @accent; padding: 0 6px; }
QPushButton#Flat:hover { text-decoration: underline; }
QPushButton#Danger { color: @record; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 3px; color: @text; }
QToolButton:hover { background: @alt; border-color: @border; }
QToolButton:focus { border-color: @accent; }
QToolButton:checked { background: @selected; border-color: @accent; }

/* Inputs */
QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit, QKeySequenceEdit, QFontComboBox {
    background: @alt; border: 1px solid @border; border-radius: 6px; padding: 0 8px; min-height: 26px; color: @text; }
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover, QLineEdit:hover { border-color: @dimborder; }
QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QLineEdit:focus, QKeySequenceEdit:focus { border-color: @accent; }
QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QLineEdit:disabled { color: @dim; background: @surface; }
QLineEdit[readOnly="true"] { color: @dim; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow { image: url(:/ui/chevron-down.svg); width: 11px; height: 11px; }
QComboBox::down-arrow:disabled { image: none; }
QAbstractSpinBox { padding-right: 20px; }
QAbstractSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 18px; border: none; }
QAbstractSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 18px; border: none; }
QAbstractSpinBox::up-arrow { image: url(:/ui/chevron-up.svg); width: 9px; height: 9px; }
QAbstractSpinBox::down-arrow { image: url(:/ui/chevron-down.svg); width: 9px; height: 9px; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: @border; }
QComboBox QAbstractItemView { background: @surface; border: 1px solid @border; selection-background-color: @accent;
                              selection-color: white; outline: 0; padding: 3px; }
QPlainTextEdit, QTextBrowser { background: @alt; border: 1px solid @border; border-radius: 6px; padding: 6px; }
QCheckBox { spacing: 8px; }
QCheckBox:focus { color: @accent; }
QRadioButton:focus { color: @accent; }

/* Lists */
QListWidget, QListView { background: transparent; border: none; outline: 0; }
QListWidget::item, QListView::item { border-radius: 6px; }
QListWidget#Nav { background: @surface; border: 1px solid @border; border-radius: 8px; padding: 6px; }
QListWidget#Nav::item { padding: 7px 10px; margin: 1px 0; color: @dim; }
QListWidget#Nav::item:hover { background: @alt; color: @text; }
QListWidget#Nav::item:selected { background: @selected; color: @text; }

/* Sliders */
QSlider::groove:horizontal { height: 4px; background: @border; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { background: @text; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }
QSlider::handle:horizontal:focus { background: @accent; }
QSlider::sub-page:horizontal:disabled { background: @border; }

/* Group boxes, tabs, scroll areas */
QGroupBox { border: 1px solid @border; border-radius: 8px; margin-top: 16px; padding: 12px 10px 8px 10px; background: @surface; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; top: 1px; padding: 0 4px; color: @dim; font-weight: 600; }
QTabWidget::pane { border: none; top: 0; background: transparent; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; padding: 7px 10px; border: none; border-bottom: 2px solid transparent; color: @dim; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; }
QTabBar::tab:hover { color: @text; }
QTabBar::tab:focus { color: @accent; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 9px; margin: 2px; }
QScrollBar::handle:vertical { background: @border; border-radius: 3px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: @dimborder; }
QScrollBar:horizontal { background: transparent; height: 9px; margin: 2px; }
QScrollBar::handle:horizontal { background: @border; border-radius: 3px; min-width: 30px; }
QScrollBar::handle:horizontal:hover { background: @dimborder; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* Banner, status bar, menus */
QFrame#Banner { border-radius: 6px; border: 1px solid @border; background: @alt; }
QFrame#Banner[kind="error"] { border-color: @record; }
QFrame#Banner[kind="warning"] { border-color: @warn; }
QFrame#Banner[kind="success"] { border-color: @success; }
QStatusBar { color: @dim; background: @window; border-top: 1px solid @border; }
QStatusBar QLabel { color: @dim; }
QStatusBar::item { border: none; }
QMenu { background: @surface; border: 1px solid @border; padding: 4px; border-radius: 6px; }
QMenu::item { padding: 6px 18px; border-radius: 4px; }
QMenu::item:selected { background: @accent; color: white; }
QMessageBox QLabel { min-width: 320px; }
)");
    const QColor dimBorder = theme == Theme::Light ? QColor(0xB9, 0xC0, 0xCC) : QColor(0x46, 0x4D, 0x5B);
    const QColor accentHover = p.accent.lighter(112);
    // Longest names first so "@accenthover" is not clobbered by "@accent".
    const std::pair<const char*, QColor> vars[] = {
        {"@accenthover", accentHover}, {"@dimborder", dimBorder}, {"@selected", p.selected}, {"@window", p.window},
        {"@surface", p.surface},       {"@success", p.success},   {"@record", p.record},     {"@accent", p.accent},
        {"@border", p.border},         {"@raised", p.raised},     {"@alt", p.surfaceAlt},    {"@text", p.text},
        {"@warn", p.warning},          {"@dim", p.textDim},
    };
    for (const auto& [name, color] : vars)
        qss.replace(QString::fromLatin1(name), color.name());
    app.setStyleSheet(qss);
}

} // namespace luma::app
