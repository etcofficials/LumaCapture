// LumaCapture - lightweight screen recorder for older Windows PCs.
//
// Automation switches (used by scripts/test-gui.ps1; they never modify the
// user's settings or recording folder):
//   --selftest <seconds> --output <dir>   scripted record/pause/resume/stop + webcam checks, then exit
//   --ui-snapshot <dir>                   render LumaCapture's own windows to PNG, then exit

#include "AppPaths.h"
#include "AppSettings.h"
#include "CrashHandler.h"
#include "MainWindow.h"
#include "Theme.h"
#include "util/Log.h"

#include <QApplication>
#include <QDate>
#include <QMessageBox>

#include <windows.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LumaCapture"));
    QApplication::setApplicationVersion(QStringLiteral(LUMACAPTURE_VERSION));
    QApplication::setQuitOnLastWindowClosed(false); // the tray icon may keep the app alive

    using namespace luma;
    app::AutomationOptions automation;
    const QStringList args = QApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == QStringLiteral("--selftest") && i + 1 < args.size())
            automation.selfTestSeconds = args[++i].toInt();
        else if (args[i] == QStringLiteral("--output") && i + 1 < args.size())
            automation.outputDir = args[++i];
        else if (args[i] == QStringLiteral("--ui-snapshot") && i + 1 < args.size())
            automation.snapshotDir = args[++i];
    }
    if (automation.selfTestSeconds > 0 && automation.outputDir.isEmpty()) {
        std::fprintf(stderr, "--selftest needs --output <dir>\n");
        return 2;
    }

    // One interactive instance only: two copies would fight over hotkeys, the camera and audio devices.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\LumaCapture.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (automation.selfTestSeconds <= 0 && automation.snapshotDir.isEmpty())
            QMessageBox::information(nullptr, QStringLiteral("LumaCapture"),
                                     QStringLiteral("LumaCapture is already running (check the notification area)."));
        CloseHandle(mutex);
        return 3;
    }

    app::crash::install(app::AppPaths::dataDir() + QStringLiteral("/crashdumps"));
    log::openFile((app::AppPaths::logDir() + QStringLiteral("/lumacapture-%1.log")
                                                  .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))))
                      .toStdWString(),
                  true);
    log::info("LumaCapture {} starting{}", LUMACAPTURE_VERSION,
              automation.selfTestSeconds > 0 ? " (self-test)" : (automation.snapshotDir.isEmpty() ? "" : " (UI snapshots)"));
    app::crash::startHangWatchdog();

    const app::AppSettings initial = app::loadSettings(app::AppPaths::settingsFile());
    app::applyTheme(app, initial.theme);

    int rc = 0;
    {
        app::MainWindow window(automation);
        window.show();
        rc = app.exec();
    }
    app::crash::stopHangWatchdog();
    log::info("LumaCapture exiting (code {})", rc);
    log::closeFile();
    if (mutex)
        CloseHandle(mutex);
    return rc;
}
