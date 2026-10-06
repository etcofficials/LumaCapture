#pragma once

#include <QString>

namespace luma::app {

// Where LumaCapture keeps its own files:
//   <exe dir>/LumaCapture-data      when that folder is writable - the portable ZIP, or
//                                   an install into a user folder such as G:\Apps\LumaCapture;
//   %LOCALAPPDATA%/LumaCapture      otherwise (e.g. installed under Program Files).
// The location is decided once at startup and cached.
//   settings.ini, history.json, logs/, crashdumps/, thumbnails/
struct AppPaths {
    static QString dataDir();
    static QString settingsFile();
    static QString historyFile();
    static QString logDir();
    static QString crashDumpDir();
    static QString thumbnailDir();
    static QString licensesDir(); // <exe dir>/licenses (shipped with the app)
    static QString docsDir();     // <exe dir> (README.txt, user guide)
};

} // namespace luma::app
