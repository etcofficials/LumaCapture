#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>

namespace luma::app {

// Hides (or shows) a top-level window from screen capture (Desktop Duplication,
// Windows.Graphics.Capture, BitBlt) via WDA_EXCLUDEFROMCAPTURE (Windows 10 2004+).
// The window stays visible on the monitor. Call it BEFORE the window is first shown
// so no frame of it can be captured. Returns false when Windows refused (older
// builds), in which case the caller must keep the window out of the way itself
// (LumaCapture then minimises its window while recording a screen).
bool setExcludedFromCapture(QWidget* window, bool excluded);

// Native dark (or light) title bar on Windows 10 1809+ / 11.
void setDarkTitleBar(QWidget* window, bool dark);

// Free bytes on the volume holding `path` (walks up to an existing folder); -1 if unknown.
int64_t freeDiskBytes(const QString& path);

// Replaces characters Windows does not allow in file names.
QString sanitizeFileName(QString name);

QString formatBytes(uint64_t bytes);
QString formatDuration(double seconds);

// Opens Explorer with the file selected.
void showInExplorer(const QString& file);
// Shows the Windows "Open with" dialog for a file.
void openWithDialog(const QString& file);

} // namespace luma::app
