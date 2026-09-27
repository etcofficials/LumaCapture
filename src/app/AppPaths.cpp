#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>

namespace luma::app {

QString AppPaths::dataDir()
{
    const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/LumaCapture-data");
    QDir().mkpath(dir);
    return dir;
}

QString AppPaths::settingsFile() { return dataDir() + QStringLiteral("/settings.ini"); }
QString AppPaths::historyFile() { return dataDir() + QStringLiteral("/history.json"); }

QString AppPaths::logDir()
{
    const QString dir = dataDir() + QStringLiteral("/logs");
    QDir().mkpath(dir);
    return dir;
}

QString AppPaths::licensesDir() { return QCoreApplication::applicationDirPath() + QStringLiteral("/licenses"); }

} // namespace luma::app
