#include "WinUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include <windows.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace luma::app {

void setExcludedFromCapture(QWidget* window, bool excluded)
{
    if (!window)
        return;
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    if (!SetWindowDisplayAffinity(hwnd, excluded ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE) && excluded) {
        // Older Windows 10 builds: fall back to WDA_MONITOR (window shows black in captures).
        SetWindowDisplayAffinity(hwnd, WDA_MONITOR);
    }
}

int64_t freeDiskBytes(const QString& path)
{
    QString p = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    while (!p.isEmpty() && !QFileInfo::exists(p)) {
        const int i = p.lastIndexOf('\\');
        if (i <= 2)
            break;
        p = p.left(i);
    }
    ULARGE_INTEGER avail{};
    if (!GetDiskFreeSpaceExW(reinterpret_cast<LPCWSTR>(p.utf16()), &avail, nullptr, nullptr))
        return -1;
    return static_cast<int64_t>(avail.QuadPart);
}

QString sanitizeFileName(QString name)
{
    static const QString bad = QStringLiteral("<>:\"/\\|?*");
    for (QChar& c : name)
        if (bad.contains(c) || c.unicode() < 32)
            c = '_';
    name = name.trimmed();
    while (name.endsWith('.'))
        name.chop(1);
    return name.isEmpty() ? QStringLiteral("recording") : name;
}

QString formatBytes(uint64_t bytes)
{
    const double b = static_cast<double>(bytes);
    if (b >= 1024.0 * 1024 * 1024)
        return QString::number(b / (1024.0 * 1024 * 1024), 'f', 2) + QStringLiteral(" GB");
    if (b >= 1024.0 * 1024)
        return QString::number(b / (1024.0 * 1024), 'f', 1) + QStringLiteral(" MB");
    return QString::number(b / 1024.0, 'f', 0) + QStringLiteral(" KB");
}

QString formatDuration(double seconds)
{
    const auto s = static_cast<qint64>(seconds);
    return QStringLiteral("%1:%2:%3")
        .arg(s / 3600, 2, 10, QChar('0'))
        .arg((s / 60) % 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0'));
}

void showInExplorer(const QString& file)
{
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,"), QDir::toNativeSeparators(file)});
}

} // namespace luma::app
