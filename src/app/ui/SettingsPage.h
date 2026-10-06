#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;

namespace luma::app {

class AudioPanel;
class DeviceScanner;
class RecordingController;
class SettingsStore;
class VideoPanel;

// Settings page: General, Recording, Video, Audio, Webcam, Hotkeys, Output,
// Appearance, Advanced, About & feedback. Every change applies immediately and is
// saved in the background (no OK/Cancel).
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    enum Section { General = 0, Recording, Video, Audio, Webcam, Hotkeys, Output, Appearance, Advanced, About };

    SettingsPage(SettingsStore& store, DeviceScanner& scanner, RecordingController& recording, QWidget* aboutPage,
                 QWidget* parent = nullptr);
    void showSection(Section s);
    void setBusy(bool busy);
    void setHotkeyConflicts(const QStringList& conflicts);
    void setTestLevelsActive(bool on);

signals:
    void openWebcamSettings();
    void recoveryScanRequested();
    void testLevelsToggled(bool on);
    void notify(int kind, const QString& text);

private:
    QWidget* buildGeneral();
    QWidget* buildRecording();
    QWidget* buildWebcam();
    QWidget* buildHotkeys();
    QWidget* buildOutput();
    QWidget* buildAppearance();
    QWidget* buildAdvanced();
    QWidget* scrollPage(const QString& title, QWidget* content);
    void refresh();

    SettingsStore& m_store;
    DeviceScanner& m_scanner;
    RecordingController& m_rec;
    QListWidget* m_nav = nullptr;
    QStackedWidget* m_stack = nullptr;
    VideoPanel* m_video = nullptr;
    AudioPanel* m_audio = nullptr;
    bool m_updating = false;

    // General
    QCheckBox* m_minimizeOnRecord = nullptr;
    QCheckBox* m_tray = nullptr;
    QSpinBox* m_countdown = nullptr;
    QCheckBox* m_hud = nullptr;
    QCheckBox* m_exclude = nullptr;
    QCheckBox* m_recovery = nullptr;
    // Recording
    QComboBox* m_container = nullptr;
    QCheckBox* m_keepMkv = nullptr;
    QCheckBox* m_behind = nullptr;
    QSpinBox* m_lowSpace = nullptr;
    QCheckBox* m_preview = nullptr;
    QSpinBox* m_previewFps = nullptr;
    QCheckBox* m_previewRec = nullptr;
    QSpinBox* m_crop[4] = {}; // left, top, right, bottom
    // Hotkeys
    QKeySequenceEdit* m_keys[5] = {};
    QLabel* m_hotkeyStatus = nullptr;
    // Output
    QLineEdit* m_outputDir = nullptr;
    QLineEdit* m_shotDir = nullptr;
    QLineEdit* m_pattern = nullptr;
    QCheckBox* m_shotCursor = nullptr;
    // Appearance
    QComboBox* m_theme = nullptr;
    // Advanced
    QCheckBox* m_diagnostics = nullptr;
    QCheckBox* m_gpuConvert = nullptr;
};

} // namespace luma::app
