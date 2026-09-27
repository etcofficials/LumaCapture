#pragma once

#include "AppSettings.h"
#include "DeviceScanner.h"

#include <QDialog>
#include <functional>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QKeySequenceEdit;
class QLabel;
class QListWidget;
class QSlider;
class QStackedWidget;
class QVBoxLayout;

namespace luma::app {

class AudioMonitor;
class LevelMeter;
class WebcamController;
class WebcamPreview;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    enum Page { General = 0, Screen, Video, Audio, Webcam, Hotkeys, Advanced, About };

    SettingsDialog(const AppSettings& settings, DeviceScanner& scanner, WebcamController& webcam, bool recording,
                   QWidget* parent = nullptr);
    ~SettingsDialog() override;

    void showPage(Page p);
    const AppSettings& settings() const { return m_s; }

signals:
    void applied(const luma::app::AppSettings& settings);
    void recoveryScanRequested();

private:
    QWidget* page(const QString& title, QVBoxLayout*& content);
    QWidget* buildGeneral();
    QWidget* buildScreen();
    QWidget* buildVideo();
    QWidget* buildAudio();
    QWidget* buildWebcam();
    QWidget* buildHotkeys();
    QWidget* buildAdvanced();
    QWidget* buildAbout();

    // Adds a labelled slider bound to a float: slider value = field * scale.
    QSlider* addSlider(QFormLayout* form, const QString& label, float* field, float min, float max, float scale,
                       const QString& tooltip, std::function<void()> onChange);
    void webcamFiltersChanged();
    void fillAudioDevices();
    void fillCameras();
    void fillCameraModes(const QString& link, const QList<CameraModeEntry>& modes, const QString& error);
    void rebuildCameraControls();
    void syncWebcamSliders();
    void updateHotkeyConflicts();
    void apply();

    AppSettings m_s;
    AppSettings m_original;
    DeviceScanner& m_scanner;
    WebcamController& m_webcam;
    AudioMonitor* m_monitor = nullptr;
    bool m_recording = false; // camera must not be restarted while a recording uses it

    QListWidget* m_nav = nullptr;
    QStackedWidget* m_stack = nullptr;

    QComboBox* m_systemDevice = nullptr;
    QComboBox* m_micDevice = nullptr;
    LevelMeter* m_sysMeter = nullptr;
    LevelMeter* m_micMeter = nullptr;

    QComboBox* m_camera = nullptr;
    QComboBox* m_cameraMode = nullptr;
    QLabel* m_cameraState = nullptr;
    WebcamPreview* m_preview = nullptr;
    QWidget* m_controlsBox = nullptr; // hardware controls container
    QFormLayout* m_controlsForm = nullptr;
    QList<CameraModeEntry> m_modes;
    std::vector<QSlider*> m_webcamSliders;
    QLabel* m_statsLabel = nullptr;
    QLabel* m_autoInfo = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QCheckBox* m_compareBox = nullptr;
    QCheckBox* m_filtersOn = nullptr;
    QCheckBox* m_monoBox = nullptr;
    bool m_controlsLoaded = false;

    QKeySequenceEdit* m_hotkeyEdits[static_cast<int>(HotkeyAction::Count)] = {};
    QLabel* m_hotkeyConflicts = nullptr;
};

} // namespace luma::app
