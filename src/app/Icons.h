#pragma once

#include <QColor>
#include <QIcon>

namespace luma::app {

// Small vector icons painted with QPainter (no image assets, crisp at any DPI).
enum class IconId {
    App, Record, Stop, Pause, Resume, Mic, MicOff, Speaker, Camera, Display, Window, Region,
    Folder, Settings, Screenshot, Play, Trash, Layout, Refresh,
    // v2
    Game, Library, Info, Search, Import, Edit, External, Chart, Sparkle, Layers, Cursor, Mail, Link, Warning,
    Help, Text, Image, Close
};

// Cached per (icon, colour): building an icon renders several pixmaps.
QIcon makeIcon(IconId id, const QColor& color);

} // namespace luma::app
