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

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QToolButton;

namespace luma::app {

class AudioMonitor;
class Banner;
class HotkeyManager;
class LayoutDialog;
class LevelMeter;
class RecordingBar;
class SettingsDialog;
class StatTile;
class ToggleSwitch;
class WebcamController;
class WebcamPreview;

struct AutomationOptions {
    int selfTestSeconds = 0;  // > 0: record, pause, resume, stop, exercise the camera, then quit
    QString snapshotDir;      // non-empty: save screenshots of the UI (own windows only) and quit
    QString outputDir;        // overrides the recording folder for the self-test (not saved)
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const AutomationOptions& automation = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    QWidget* buildSourceCard();
    QWidget* buildVideoCard();
    QWidget* buildAudioCard();
    QWidget* buildWebcamCard();
    QWidget* buildStudioPanel();
    QWidget* buildRecentPanel();
    void buildTray();

    void settingsChanged(bool live = true);
    void applySettings(const AppSettings& s); // from the settings dialog
    void syncWidgetsFromSettings();
    void updateSummary();
    void updateRecordingUi();
    void updateWarnings();
    void updateFooter();
    void refreshRecent();
    void registerHotkeys(bool showConflicts);
    void applyCaptureExclusion(QWidget* w);
    void saveSettingsNow();

    void onRecordClicked();
    void startWithPreflight();
    void onStateChanged(session::RecState s);
    void onStatusTick();
    void onHotkey(HotkeyAction a);
    void openSettings(int page);
    void openLayout();
    void selectRegion();
    void fillMonitors();
    void fillWindows(const QList<WindowEntry>& windows);
    void showError(const QString& title, const QString& message);
    void notify(int kind, const QString& text, int autoHideMs = 6000);

    void runSelfTest();
    void runSnapshots();

    AutomationOptions m_automation;
    AppSettings m_settings;
    History m_history;
    DeviceScanner* m_scanner = nullptr;
    WebcamController* m_webcam = nullptr;
    RecordingController* m_controller = nullptr;
    HotkeyManager* m_hotkeys = nullptr;
    AudioMonitor* m_audioTest = nullptr;
    RecordingBar* m_bar = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QAction* m_trayRecord = nullptr;
    QPointer<SettingsDialog> m_settingsDialog;
    QPointer<LayoutDialog> m_layoutDialog;
    QTimer m_saveTimer;
    QTimer m_liveTimer;
    QTimer m_heartbeat;
    QTimer m_finishTicker;
    bool m_quitAfterStop = false;
    bool m_loadingUi = false;
    bool m_hotkeysRegistered = false;
    QString m_savedHint;

    // Source
    QButtonGroup* m_sourceGroup = nullptr;
    QStackedWidget* m_sourceStack = nullptr;
    QComboBox* m_display = nullptr;
    QComboBox* m_window = nullptr;
    QLabel* m_regionLabel = nullptr;
    QComboBox* m_regionAspect = nullptr;
    QSpinBox* m_regionX = nullptr;
    QSpinBox* m_regionY = nullptr;
    QSpinBox* m_regionW = nullptr;
    QSpinBox* m_regionH = nullptr;
    QCheckBox* m_cursor = nullptr;
    QWidget* m_sourceCard = nullptr;
    QLabel* m_sourceWarning = nullptr;
    // Video
    QComboBox* m_resolution = nullptr;
    QComboBox* m_fps = nullptr;
    QComboBox* m_quality = nullptr;
    QComboBox* m_speed = nullptr;
    QLabel* m_videoWarning = nullptr;
    QWidget* m_videoCard = nullptr;
    // Audio
    ToggleSwitch* m_systemAudio = nullptr;
    ToggleSwitch* m_mic = nullptr;
    QLabel* m_systemName = nullptr;
    QLabel* m_micName = nullptr;
    QSlider* m_systemVolume = nullptr;
    QSlider* m_micVolume = nullptr;
    LevelMeter* m_systemMeter = nullptr;
    LevelMeter* m_micMeter = nullptr;
    QToolButton* m_micMute = nullptr;
    QPushButton* m_audioTestButton = nullptr;
    // Webcam
    ToggleSwitch* m_webcamOn = nullptr;
    QLabel* m_webcamName = nullptr;
    QLabel* m_webcamState = nullptr;
    WebcamPreview* m_webcamPreview = nullptr;
    // Studio
    QLabel* m_pill = nullptr;
    QLabel* m_timer = nullptr;
    QLabel* m_stateText = nullptr;
    StatTile* m_tileOutput = nullptr;
    StatTile* m_tileFps = nullptr;
    StatTile* m_tileDropped = nullptr;
    StatTile* m_tileSize = nullptr;
    QPushButton* m_record = nullptr;
    QPushButton* m_pause = nullptr;
    QPushButton* m_screenshot = nullptr;
    QLabel* m_summary = nullptr;
    Banner* m_banner = nullptr;
    QComboBox* m_profile = nullptr;
    QLabel* m_footerPath = nullptr;
    QLabel* m_footerFree = nullptr;
    // Recent
    QListWidget* m_recent = nullptr;
    QPushButton* m_openBtn = nullptr;
    QPushButton* m_showBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;

    int64_t m_lastFrames = 0;
    QElapsedTimer m_fpsClock;
    double m_measuredFps = 0;
};

} // namespace luma::app
