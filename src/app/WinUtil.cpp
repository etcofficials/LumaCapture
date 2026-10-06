#include "WinUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include <windows.h>
#include <dwmapi.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace luma::app {

bool setExcludedFromCapture(QWidget* window, bool excluded)
{
    if (!window)
        return false;
    const auto hwnd = reinterpret_cast<HWND>(window->winId()); // creates the native window if needed
    if (!excluded) {
        SetWindowDisplayAffinity(hwnd, WDA_NONE);
        return true;
    }
    if (SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE))
        return true;
    // Windows 10 before 2004 has no WDA_EXCLUDEFROMCAPTURE. WDA_MONITOR would show a
    // black rectangle in recordings instead, which is no better - leave it unset.
    return false;
}

void setDarkTitleBar(QWidget* window, bool dark)
{
    if (!window)
        return;
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL value = dark ? TRUE : FALSE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE is 20 on Windows 10 2004+ and 19 on 1809-1909.
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &value, sizeof(value))))
        DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value));
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

void openWithDialog(const QString& file)
{
    QProcess::startDetached(QStringLiteral("rundll32.exe"),
                            {QStringLiteral("shell32.dll,OpenAs_RunDLL"), QDir::toNativeSeparators(file)});
}

} // namespace luma::app
