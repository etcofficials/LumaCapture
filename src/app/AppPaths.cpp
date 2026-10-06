#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace luma::app {
namespace {

bool canWriteIn(const QString& dir)
{
    if (!QDir().mkpath(dir))
        return false;
    QFile probe(dir + QStringLiteral("/.write-test"));
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
}

QString subDir(const QString& name)
{
    const QString dir = AppPaths::dataDir() + QLatin1Char('/') + name;
    QDir().mkpath(dir);
    return dir;
}

} // namespace

QString AppPaths::dataDir()
{
    static const QString dir = [] {
        const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/LumaCapture-data");
        if (canWriteIn(beside))
            return beside;
        QString local = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        if (local.isEmpty())
            local = QDir::homePath();
        local += QStringLiteral("/LumaCapture");
        QDir().mkpath(local);
        return local;
    }();
    return dir;
}

QString AppPaths::settingsFile() { return dataDir() + QStringLiteral("/settings.ini"); }
QString AppPaths::historyFile() { return dataDir() + QStringLiteral("/history.json"); }

QString AppPaths::logDir()
{
    static const QString dir = subDir(QStringLiteral("logs"));
    return dir;
}

QString AppPaths::crashDumpDir()
{
    static const QString dir = subDir(QStringLiteral("crashdumps"));
    return dir;
}

QString AppPaths::thumbnailDir()
{
    static const QString dir = subDir(QStringLiteral("thumbnails"));
    return dir;
}

QString AppPaths::licensesDir() { return QCoreApplication::applicationDirPath() + QStringLiteral("/licenses"); }
QString AppPaths::docsDir() { return QCoreApplication::applicationDirPath(); }

} // namespace luma::app
