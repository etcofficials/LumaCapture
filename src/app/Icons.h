#pragma once

#include <QColor>
#include <QIcon>

namespace luma::app {

// Small vector icons painted with QPainter (no image assets, crisp at any DPI).
enum class IconId {
    App, Record, Stop, Pause, Resume, Mic, MicOff, Speaker, Camera, Display, Window, Region,
    Folder, Settings, Screenshot, Play, Trash, Layout, Refresh
};

QIcon makeIcon(IconId id, const QColor& color);

} // namespace luma::app
