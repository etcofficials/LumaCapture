#pragma once

#include <QString>

namespace luma::app {

// Portable layout: everything LumaCapture writes lives next to the executable
// (on G: for this installation), never in %APPDATA%.
//   <exe dir>/LumaCapture-data/settings.ini
//   <exe dir>/LumaCapture-data/history.json
//   <exe dir>/LumaCapture-data/logs/
struct AppPaths {
    static QString dataDir();
    static QString settingsFile();
    static QString historyFile();
    static QString logDir();
    static QString licensesDir(); // <exe dir>/licenses (shipped with the app)
};

} // namespace luma::app
