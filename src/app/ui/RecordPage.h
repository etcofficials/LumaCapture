#pragma once

#include "AppSettings.h"
#include "DeviceScanner.h"
#include "session/RecordingSession.h"
#include "session/RecordingState.h"

#include <QWidget>

class QButtonGroup;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QToolButton;

namespace luma::app {

class AudioPanel;
class EffectsPanel;
class History;
class LevelMeter;
class OverlaysPanel;
class PreviewController;
class PreviewView;
class RecordingController;
class SettingsStore;
class StatTile;
class ThumbnailCache;
class ToggleSwitch;
class TransportButton;
class VideoPanel;
class WebcamController;
class WebcamPanel;

struct DiagnosticsSample {
    double processCpu = -1; // % of the whole machine; -1 = unknown
    double systemCpu = -1;
    double gpu = -1;        // busiest GPU engine, all processes
    double ramMb = 0;       // LumaCapture working set
};

// The Record page: capture mode, sources, output and quick settings on the left;
// live preview, transport controls, meters and recent recordings in the centre;
// contextual settings (Video, Audio, Webcam, Effects, Overlays) on the right.
class RecordPage : public QWidget {
    Q_OBJECT
public:
    enum Tab { VideoTab = 0, AudioTab, WebcamTab, EffectsTab, OverlaysTab };

    RecordPage(SettingsStore& store, DeviceScanner& scanner, WebcamController& webcam, RecordingController& recording,
               PreviewController& preview, History& history, ThumbnailCache& thumbs, QWidget* parent = nullptr);

    void setRecordingState(session::RecState state);
    void setStatus(const session::SessionStatus& status);
    void setAudioLevels(float system, float mic);
    void resetLevels();
    void setFinishingText(const QString& text);
    void setTestLevelsActive(bool on);
    void setDiagnosticsVisible(bool visible);
    void setDiagnostics(const DiagnosticsSample& sample);
    void refreshRecent();
    void fillMonitors();
    void fillWindows(const QList<WindowEntry>& windows);
    void showContextTab(Tab tab, bool expandAdvanced = false);
    int currentContextTab() const;
    PreviewView* previewView() const { return m_previewView; }
    WebcamPanel* webcamPanel() const { return m_webcamPanel; }

signals:
    void recordClicked();
    void pauseClicked();
    void stopClicked();
    void screenshotClicked();
    void selectRegionRequested(double aspect); // 0 = free
    void openLibraryRequested();
    void openLayoutEditorRequested();
    void testLevelsToggled(bool on);
    void refreshDevicesRequested();
    void contextTabChanged(int tab);
    void notify(int kind, const QString& text);

private:
    QWidget* buildLeft();
    QWidget* buildCenter();
    QWidget* buildRight();
    QWidget* buildCaptureMode(QWidget* parent);
    QWidget* buildSources(QWidget* parent);
    QWidget* buildOutput(QWidget* parent);
    QWidget* buildQuickSettings(QWidget* parent);
    QWidget* buildTransport(QWidget* parent);
    QWidget* buildDiagnostics(QWidget* parent);
    QWidget* buildRecent(QWidget* parent);
    void refreshFromSettings(unsigned scope);
    void updateSummary();
    void updateTransport();
    void chooseOutputFolder();

    SettingsStore& m_store;
    DeviceScanner& m_scanner;
    WebcamController& m_webcam;
    RecordingController& m_rec;
    PreviewController& m_preview;
    History& m_history;
    ThumbnailCache& m_thumbs;
    session::RecState m_state = session::RecState::Idle;
    bool m_updating = false;

    // Left
    QButtonGroup* m_modeGroup = nullptr;
    QStackedWidget* m_targetStack = nullptr;
    QComboBox* m_display = nullptr;
    QComboBox* m_window = nullptr;
    QComboBox* m_game = nullptr;
    QLabel* m_regionLabel = nullptr;
    QComboBox* m_regionAspect = nullptr;
    QLabel* m_sourceWarning = nullptr;
    QLabel* m_srcDisplayName = nullptr;
    QLabel* m_srcDisplayInfo = nullptr;
    ToggleSwitch* m_srcWebcam = nullptr;
    QLabel* m_srcWebcamName = nullptr;
    ToggleSwitch* m_srcMic = nullptr;
    QLabel* m_srcMicName = nullptr;
    ToggleSwitch* m_srcSystem = nullptr;
    QLabel* m_srcSystemName = nullptr;
    QLineEdit* m_outputDir = nullptr;
    QLineEdit* m_namePattern = nullptr;
    QComboBox* m_container = nullptr;
    QComboBox* m_profile = nullptr;
    QComboBox* m_qsResolution = nullptr;
    QComboBox* m_qsFps = nullptr;
    QComboBox* m_qsQuality = nullptr;
    QLabel* m_qsPerf = nullptr;
    QWidget* m_leftContent = nullptr;

    // Centre
    PreviewView* m_previewView = nullptr;
    QLabel* m_stateText = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_timer = nullptr;
    QLabel* m_timerState = nullptr;
    TransportButton* m_record = nullptr;
    TransportButton* m_pause = nullptr;
    TransportButton* m_stop = nullptr;
    TransportButton* m_screenshot = nullptr;
    LevelMeter* m_systemMeter = nullptr;
    LevelMeter* m_micMeter = nullptr;
    QToolButton* m_micMute = nullptr;
    QPushButton* m_testLevels = nullptr;
    QWidget* m_diagnostics = nullptr;
    StatTile* m_dFps = nullptr;
    StatTile* m_dDropped = nullptr;
    StatTile* m_dRepeated = nullptr;
    StatTile* m_dCpu = nullptr;
    StatTile* m_dRam = nullptr;
    StatTile* m_dGpu = nullptr;
    StatTile* m_dCapLat = nullptr;
    StatTile* m_dEncLat = nullptr;
    StatTile* m_dQueue = nullptr;
    StatTile* m_dBitrate = nullptr;
    QListWidget* m_recent = nullptr;
    int64_t m_lastBytes = 0;
    int64_t m_lastEncoded = 0;
    double m_lastElapsed = 0;

    // Right
    QTabWidget* m_tabs = nullptr;
    VideoPanel* m_videoPanel = nullptr;
    AudioPanel* m_audioPanel = nullptr;
    WebcamPanel* m_webcamPanel = nullptr;
    EffectsPanel* m_effectsPanel = nullptr;
    OverlaysPanel* m_overlaysPanel = nullptr;
};

} // namespace luma::app
