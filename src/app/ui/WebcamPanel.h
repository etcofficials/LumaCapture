#pragma once

#include "DeviceScanner.h"
#include "ui/UiKit.h"
#include "webcam/WebcamFilters.h"

#include <QTimer>
#include <QWidget>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace luma::app {

class ColorButton;
class SettingsStore;
class WebcamController;
class WebcamPreview;

// Right-hand "Webcam" tab: camera and mode, live preview, Auto adjust, presets,
// before/after, image filters, the camera's own hardware controls (anti-flicker,
// low-light behaviour, exposure, white balance, ...) and the green screen.
// Only controls the camera actually reports are shown.
class WebcamPanel : public QWidget {
    Q_OBJECT
public:
    WebcamPanel(SettingsStore& store, DeviceScanner& scanner, WebcamController& webcam, QWidget* parent = nullptr);
    void refresh();
    void setBusy(bool busy);
    // The tab is visible: keep the camera running for configuring and show its preview.
    void setActive(bool active);

private:
    void fillCameras();
    void fillModes(const QString& link, const QList<CameraModeEntry>& modes, const QString& error);
    void rebuildControls();
    void editFilters(const std::function<void(webcam::WebcamFilterSettings&)>& change);
    void updateStats();

    struct FilterSlider {
        ui::SliderRow row;
        float webcam::WebcamFilterSettings::*field = nullptr;
    };

    SettingsStore& m_store;
    DeviceScanner& m_scanner;
    WebcamController& m_webcam;
    WebcamPreview* m_preview = nullptr;
    QLabel* m_state = nullptr;
    QLabel* m_stats = nullptr;
    QLabel* m_autoInfo = nullptr;
    QComboBox* m_camera = nullptr;
    QComboBox* m_mode = nullptr;
    QComboBox* m_presets = nullptr;
    QCheckBox* m_compare = nullptr;
    QCheckBox* m_filtersOn = nullptr;
    QCheckBox* m_mirror = nullptr;
    std::vector<FilterSlider> m_sliders;
    QCheckBox* m_mono = nullptr;
    QWidget* m_controlsBox = nullptr;
    QVBoxLayout* m_controlsLayout = nullptr;
    QCheckBox* m_key = nullptr;
    ColorButton* m_keyColor = nullptr;
    ui::SliderRow m_similarity, m_smooth, m_spill;
    QLineEdit* m_background = nullptr;
    QList<CameraModeEntry> m_modes;
    QTimer m_statsTimer;
    bool m_controlsLoaded = false;
    bool m_updating = false;
    bool m_busy = false;
    bool m_active = false;
};

} // namespace luma::app
