#pragma once

#include "AppSettings.h"
#include "DeviceScanner.h"
#include "History.h"
#include "RecordingController.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTimer>

#include <memory>

class QButtonGroup;
class QLabel;
class QStackedWidget;

namespace luma::app {

class AboutPage;
class AudioMonitor;
class Banner;
class HotkeyManager;
class LayoutDialog;
class LibraryPage;
class MediaImporter;
class PreviewController;
class RecordPage;
class RecordingBar;
class SettingsPage;
class SettingsStore;
class ThumbnailCache;
class WebcamController;
struct DiagnosticsState;

struct AutomationOptions {
    int selfTestSeconds = 0;  // > 0: record, pause, resume, stop, exercise the camera, then quit
    QString snapshotDir;      // non-empty: save screenshots of the UI (own windows only) and quit
    QString outputDir;        // overrides the recording folder for the self-test (not saved)
};

// Top-level window: navigation (Record / Library / Settings / About), the shared
// notification banner and status bar, and the orchestration of recording
// (preflight, live preview hand-over, countdown, capture exclusion, HUD, tray,
// hotkeys). All heavy work lives in the controllers and runs off the UI thread.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const AutomationOptions& automation = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Registered window message a second instance broadcasts to bring this window up.
    static unsigned activateMessageId();

protected:
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    enum Page { RecordPg = 0, LibraryPg, SettingsPg, AboutPg };

    QWidget* buildTopBar();
    void buildTray();
    void showPage(Page p);
    void onSettingsChanged(unsigned scope);
    void applyWebcam();
    void registerHotkeys(bool showConflicts);
    void updatePreviewVisibility();
    bool exclude(QWidget* w);
    void updateFooter();
    void bringToFront();

    void onRecordClicked();
    void startWithPreflight();
    void continueStart();
    void beginRecording();
    void onStateChanged(session::RecState s);
    void onStatusTick();
    void onHotkey(HotkeyAction a);
    void selectRegion(double aspect);
    void openLayoutEditor();
    void setTestLevels(bool on);
    void sampleDiagnostics();
    void notify(int kind, const QString& text, int autoHideMs = 6000);
    void showError(const QString& title, const QString& message);

    void runSelfTest();
    void runSnapshots();

    AutomationOptions m_automation;
    SettingsStore* m_store = nullptr;
    History m_history;
    DeviceScanner* m_scanner = nullptr;
    WebcamController* m_webcam = nullptr;
    RecordingController* m_controller = nullptr;
    PreviewController* m_preview = nullptr;
    HotkeyManager* m_hotkeys = nullptr;
    AudioMonitor* m_audioTest = nullptr;
    ThumbnailCache* m_thumbs = nullptr;
    MediaImporter* m_importer = nullptr;
    RecordingBar* m_hud = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QAction* m_trayRecord = nullptr;
    QPointer<LayoutDialog> m_layoutDialog;
    std::shared_ptr<DiagnosticsState> m_diag;

    QButtonGroup* m_nav = nullptr;
    QLabel* m_topState = nullptr;
    Banner* m_banner = nullptr;
    QStackedWidget* m_pages = nullptr;
    RecordPage* m_recordPage = nullptr;
    LibraryPage* m_libraryPage = nullptr;
    SettingsPage* m_settingsPage = nullptr;
    AboutPage* m_aboutPage = nullptr;
    QLabel* m_footerPath = nullptr;
    QLabel* m_footerFree = nullptr;

    QTimer m_liveTimer;    // debounced live updates to a running recording
    QTimer m_meterTimer;   // ~25 Hz audio meters while recording
    QTimer m_heartbeat;    // UI hang watchdog
    QTimer m_finishTicker; // "saving (n s)" text
    QTimer m_diagTimer;    // 1 Hz diagnostics (only when the panel is on)
    QString m_savedHint;
    bool m_quitAfterStop = false;
    bool m_hotkeysRegistered = false;
    bool m_excludeOk = true;          // Windows accepted WDA_EXCLUDEFROMCAPTURE for the main window
    bool m_restoreAfterRecording = false;
    bool m_diagBusy = false;
};

} // namespace luma::app
