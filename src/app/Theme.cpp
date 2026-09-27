#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace luma::app {
namespace {

Palette g_palette;

Palette makePalette(Theme t)
{
    Palette p;
    if (t == Theme::Light) {
        p.window = QColor(0xF4, 0xF5, 0xF8);
        p.surface = QColor(0xFF, 0xFF, 0xFF);
        p.surfaceAlt = QColor(0xEE, 0xF0, 0xF4);
        p.border = QColor(0xD9, 0xDD, 0xE4);
        p.text = QColor(0x1A, 0x1D, 0x24);
        p.textDim = QColor(0x62, 0x6A, 0x78);
        p.accent = QColor(0x25, 0x63, 0xEB);
    } else {
        p.window = QColor(0x12, 0x14, 0x19);
        p.surface = QColor(0x1A, 0x1D, 0x24);
        p.surfaceAlt = QColor(0x23, 0x27, 0x30);
        p.border = QColor(0x2E, 0x33, 0x3E);
        p.text = QColor(0xE7, 0xE9, 0xEE);
        p.textDim = QColor(0x9A, 0xA2, 0xB0);
        p.accent = QColor(0x4C, 0x8D, 0xFF);
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
* { font-family: "Segoe UI"; font-size: 9.5pt; }
QMainWindow, QDialog { background: @window; }
QToolTip { background: @surface; color: @text; border: 1px solid @border; padding: 5px 7px; border-radius: 6px; }

/* Cards and typography */
QFrame#Card { background: @surface; border: 1px solid @border; border-radius: 12px; }
QLabel#SectionTitle { color: @dim; font-size: 8.5pt; font-weight: 600; letter-spacing: 0.6px; }
QLabel#PageTitle { color: @text; font-size: 14pt; font-weight: 600; }
QLabel#Brand { color: @text; font-size: 13pt; font-weight: 600; }
QLabel#Dim { color: @dim; }
QLabel#Hint { color: @dim; font-size: 8.5pt; }
QLabel#Warn { color: @warn; }
QLabel#Timer { color: @text; font-size: 30pt; font-weight: 300; }
QLabel#TileCaption { color: @dim; font-size: 8pt; }
QLabel#TileValue { color: @text; font-size: 10.5pt; font-weight: 600; }
QLabel#TileValue[warn="true"] { color: @warn; }
QLabel#Pill { border-radius: 10px; padding: 2px 10px; font-size: 8.5pt; font-weight: 700; letter-spacing: 0.5px;
              background: @alt; color: @dim; }
QLabel#Pill[state="rec"] { background: @record; color: white; }
QLabel#Pill[state="paused"] { background: @warn; color: #1a1300; }
QLabel#Pill[state="busy"] { background: @accent; color: white; }
QLabel#Pill[state="error"] { background: @record; color: white; }

/* Buttons */
QPushButton { background: @alt; border: 1px solid @border; border-radius: 8px; padding: 0 14px; min-height: 30px; color: @text; }
QPushButton:hover { border-color: @accent; }
QPushButton:pressed { background: @border; }
QPushButton:focus { border: 1px solid @accent; }
QPushButton:disabled { color: @dim; background: @surface; border-color: @border; }
QPushButton:checked { background: @accent; border-color: @accent; color: white; }
QPushButton#Primary { background: @accent; border: 1px solid @accent; color: white; font-weight: 600; }
QPushButton#Primary:hover { background: @accenthover; }
QPushButton#Flat { background: transparent; border: none; color: @accent; font-weight: 600; padding: 0 6px; }
QPushButton#Record { background: @record; border: 2px solid @record; color: white; font-size: 11pt; font-weight: 600;
                     min-height: 44px; border-radius: 22px; padding: 0 26px; }
QPushButton#Record:hover { background: #F05A5F; border-color: #F05A5F; }
QPushButton#Record:focus { border: 2px solid @text; }
QPushButton#Record[recording="true"] { background: @surface; color: @record; border: 2px solid @record; }
QPushButton#Record:disabled { background: @alt; border-color: @border; color: @dim; }
QPushButton#Round { min-height: 44px; min-width: 44px; max-width: 44px; border-radius: 22px; padding: 0; }
QPushButton#Seg { border-radius: 0; margin: 0; }
QPushButton#SegFirst { border-top-right-radius: 0; border-bottom-right-radius: 0; margin: 0; }
QPushButton#SegLast { border-top-left-radius: 0; border-bottom-left-radius: 0; margin: 0; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 8px; padding: 4px; color: @text; min-height: 22px; }
QToolButton:hover { background: @alt; border-color: @border; }
QToolButton:focus { border-color: @accent; }
QToolButton:checked { background: @alt; border-color: @record; }

/* Inputs */
QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit, QKeySequenceEdit, QFontComboBox {
    background: @alt; border: 1px solid @border; border-radius: 8px; padding: 0 8px; min-height: 30px; color: @text; }
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover, QLineEdit:hover { border-color: @dimborder; }
QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QLineEdit:focus, QKeySequenceEdit:focus { border-color: @accent; }
QComboBox:disabled, QSpinBox:disabled, QLineEdit:disabled { color: @dim; background: @surface; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: url(:/ui/chevron-down.svg); width: 12px; height: 12px; }
QComboBox::down-arrow:disabled { image: none; }
QAbstractSpinBox { padding-right: 22px; }
QAbstractSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 20px; border: none; }
QAbstractSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 20px; border: none; }
QAbstractSpinBox::up-arrow { image: url(:/ui/chevron-up.svg); width: 10px; height: 10px; }
QAbstractSpinBox::down-arrow { image: url(:/ui/chevron-down.svg); width: 10px; height: 10px; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: @border; }
QComboBox QAbstractItemView { background: @surface; border: 1px solid @border; selection-background-color: @accent;
                              selection-color: white; outline: 0; padding: 4px; }
QPlainTextEdit, QTextBrowser { background: @alt; border: 1px solid @border; border-radius: 8px; padding: 6px; }
QCheckBox { spacing: 8px; }
QCheckBox:focus { color: @accent; }

/* Lists */
QListWidget { background: transparent; border: none; outline: 0; }
QListWidget::item { border-radius: 8px; padding: 2px; }
QListWidget::item:hover { background: @alt; }
QListWidget::item:selected { background: @selected; color: @text; }
QListWidget#Nav { background: @surface; border: 1px solid @border; border-radius: 12px; padding: 6px; }
QListWidget#Nav::item { padding: 8px 10px; margin: 1px 0; }
QListWidget#Nav::item:selected { background: @accent; color: white; }

/* Sliders */
QSlider::groove:horizontal { height: 4px; background: @border; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { background: @text; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }
QSlider::handle:horizontal:focus { background: @accent; }
QSlider::sub-page:horizontal:disabled { background: @border; }

/* Group boxes, tabs, scroll areas */
QGroupBox { border: 1px solid @border; border-radius: 12px; margin-top: 18px; padding: 14px 12px 10px 12px; background: @surface; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; top: 2px; padding: 0 4px; color: @dim; font-weight: 600; }
QTabWidget::pane { border: 1px solid @border; border-radius: 10px; top: -1px; background: @surface; }
QTabBar::tab { background: transparent; padding: 7px 14px; border: none; color: @dim; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; }
QTabBar::tab:hover { color: @text; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @border; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: @dimborder; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar:horizontal { height: 0; }

/* Banner */
QFrame#Banner { border-radius: 10px; border: 1px solid @border; background: @alt; }
QFrame#Banner[kind="error"] { border-color: @record; }
QFrame#Banner[kind="warning"] { border-color: @warn; }
QFrame#Banner[kind="success"] { border-color: @success; }
QStatusBar { color: @dim; }
QStatusBar::item { border: none; }
QMenu { background: @surface; border: 1px solid @border; padding: 4px; border-radius: 8px; }
QMenu::item { padding: 6px 18px; border-radius: 6px; }
QMenu::item:selected { background: @accent; color: white; }
QMessageBox QLabel { min-width: 320px; }
)");
    const QColor dimBorder = theme == Theme::Light ? QColor(0xB9, 0xC0, 0xCC) : QColor(0x46, 0x4D, 0x5B);
    const QColor selected = theme == Theme::Light ? QColor(0xDC, 0xE7, 0xFD) : QColor(0x24, 0x33, 0x52);
    const QColor accentHover = p.accent.lighter(112);
    // Longest names first so "@accenthover" is not clobbered by "@accent".
    const std::pair<const char*, QColor> vars[] = {
        {"@accenthover", accentHover}, {"@dimborder", dimBorder}, {"@selected", selected}, {"@window", p.window},
        {"@surface", p.surface},       {"@success", p.success},   {"@record", p.record},   {"@accent", p.accent},
        {"@border", p.border},         {"@alt", p.surfaceAlt},    {"@text", p.text},       {"@warn", p.warning},
        {"@dim", p.textDim},
    };
    for (const auto& [name, color] : vars)
        qss.replace(QString::fromLatin1(name), color.name());
    app.setStyleSheet(qss);
}

} // namespace luma::app
