#pragma once

#include "AppSettings.h"

#include <QColor>

class QApplication;

namespace luma::app {

struct Palette {
    QColor window, surface, surfaceAlt, raised, border, text, textDim, accent, selected, record, warning, success;
};

const Palette& currentPalette();
// Applies the Fusion style, a palette and the LumaCapture style sheet.
// Plain colours only - no blur, gradients or animated effects (the UI must stay
// cheap to draw while a recording uses the CPU).
void applyTheme(QApplication& app, Theme theme);

} // namespace luma::app
