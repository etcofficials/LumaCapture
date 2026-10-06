#pragma once

#include "ui/UiKit.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace luma::app {

class CollapsibleSection;
class ColorButton;
class DeviceScanner;
class RecordingController;
class SettingsStore;
class ToggleSwitch;
class WebcamController;

// Combo fillers shared by the Record page's quick settings and the Video panel.
void fillResolutionCombo(QComboBox* c); // itemData = int(ResolutionPreset)
void fillFpsCombo(QComboBox* c);        // itemData = fps
void fillQualityCombo(QComboBox* c);    // itemData = int(QualityPreset)
void fillPresetCombo(QComboBox* c);     // itemData = "auto" / x264 preset name

// Right-hand "Video" tab: resolution, frame rate, quality, encoder, container,
// performance estimate, advanced encoder options and the resolution guide.
class VideoPanel : public QWidget {
    Q_OBJECT
public:
    VideoPanel(SettingsStore& store, RecordingController& recording, QWidget* parent = nullptr);
    void refresh();
    void setBusy(bool busy);

private:
    void updatePerformance();

    SettingsStore& m_store;
    RecordingController& m_rec;
    QComboBox* m_resolution = nullptr;
    QComboBox* m_fps = nullptr;
    QComboBox* m_quality = nullptr;
    QComboBox* m_preset = nullptr;
    QComboBox* m_container = nullptr;
    QLabel* m_perf = nullptr;
    QLabel* m_advice = nullptr;
    CollapsibleSection* m_advanced = nullptr;
    QSpinBox* m_crf = nullptr;
    QDoubleSpinBox* m_keyframe = nullptr;
    QSpinBox* m_threads = nullptr;
    QCheckBox* m_gpuConvert = nullptr;
    QSpinBox* m_queue = nullptr;
    QLabel* m_guide = nullptr;
    bool m_updating = false;
};

// Right-hand "Audio" tab: devices, volumes, microphone processing, encoding.
class AudioPanel : public QWidget {
    Q_OBJECT
public:
    AudioPanel(SettingsStore& store, DeviceScanner& scanner, QWidget* parent = nullptr);
    void refresh();
    void refreshDevices();
    void setBusy(bool busy);
    void setTestActive(bool on);

signals:
    void testLevelsToggled(bool on);

private:
    SettingsStore& m_store;
    DeviceScanner& m_scanner;
    ToggleSwitch* m_system = nullptr;
    QComboBox* m_systemDevice = nullptr;
    ui::SliderRow m_systemVol;
    ToggleSwitch* m_mic = nullptr;
    QComboBox* m_micDevice = nullptr;
    ui::SliderRow m_micVol;
    QCheckBox* m_micMuted = nullptr;
    QSpinBox* m_micDelay = nullptr;
    QPushButton* m_test = nullptr;
    QCheckBox* m_proc = nullptr;
    QCheckBox* m_noise = nullptr;
    ui::SliderRow m_noiseDb;
    QCheckBox* m_highPass = nullptr;
    QCheckBox* m_gate = nullptr;
    ui::SliderRow m_gateThr;
    QCheckBox* m_comp = nullptr;
    ui::SliderRow m_compThr;
    ui::SliderRow m_compRatio;
    QCheckBox* m_limiter = nullptr;
    ui::SliderRow m_gain;
    QCheckBox* m_eq = nullptr;
    ui::SliderRow m_eqLow, m_eqMid, m_eqHigh;
    QComboBox* m_bitrate = nullptr;
    QCheckBox* m_separate = nullptr;
    bool m_updating = false;
};

// Right-hand "Effects" tab: cursor effects and screen colour adjustments.
class EffectsPanel : public QWidget {
    Q_OBJECT
public:
    explicit EffectsPanel(SettingsStore& store, QWidget* parent = nullptr);
    void refresh();

private:
    SettingsStore& m_store;
    QCheckBox* m_cursor = nullptr;
    QCheckBox* m_highlight = nullptr;
    ColorButton* m_hlColor = nullptr;
    ui::SliderRow m_hlRadius;
    QCheckBox* m_clicks = nullptr;
    ColorButton* m_leftColor = nullptr;
    ColorButton* m_rightColor = nullptr;
    ui::SliderRow m_clickRadius;
    QCheckBox* m_fx = nullptr;
    ui::SliderRow m_brightness, m_contrast, m_saturation, m_gamma, m_temperature, m_tint, m_sharpen, m_blur;
    QCheckBox* m_gray = nullptr;
    bool m_updating = false;
};

// Right-hand "Overlays" tab: webcam overlay placement, text and image overlays, watermark.
class OverlaysPanel : public QWidget {
    Q_OBJECT
public:
    OverlaysPanel(SettingsStore& store, WebcamController& webcam, RecordingController& recording,
                  QWidget* parent = nullptr);
    void refresh();

signals:
    void openLayoutEditor();

private:
    void refreshList();
    void loadItem();
    void storeItem();
    int currentItem() const;
    void placeWebcam(int corner);

    SettingsStore& m_store;
    WebcamController& m_webcam;
    RecordingController& m_rec;
    ToggleSwitch* m_camOn = nullptr;
    QComboBox* m_camCorner = nullptr;
    ui::SliderRow m_camSize;
    ui::SliderRow m_camOpacity;
    QComboBox* m_camShape = nullptr;
    ui::SliderRow m_camRadius;
    QSpinBox* m_camBorder = nullptr;
    ColorButton* m_camBorderColor = nullptr;
    QCheckBox* m_camMirror = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_remove = nullptr;
    QWidget* m_editor = nullptr;
    QCheckBox* m_itemEnabled = nullptr;
    QWidget* m_textFields = nullptr;
    QLineEdit* m_text = nullptr;
    QFontComboBox* m_font = nullptr;
    QSpinBox* m_fontSize = nullptr;
    ColorButton* m_color = nullptr;
    QCheckBox* m_bold = nullptr;
    QCheckBox* m_outline = nullptr;
    QWidget* m_imageFields = nullptr;
    QLineEdit* m_imagePath = nullptr;
    ui::SliderRow m_x, m_y, m_w, m_opacity;
    QCheckBox* m_wm = nullptr;
    QLineEdit* m_wmText = nullptr;
    QComboBox* m_wmCorner = nullptr;
    ui::SliderRow m_wmOpacity;
    bool m_updating = false;
};

} // namespace luma::app
