#pragma once

#include "AppSettings.h"

#include <QColor>

class QApplication;

namespace luma::app {

struct Palette {
    QColor window, surface, surfaceAlt, border, text, textDim, accent, record, warning, success;
};

const Palette& currentPalette();
// Applies the Fusion style, a palette and the LumaCapture style sheet.
void applyTheme(QApplication& app, Theme theme);

} // namespace luma::app
