#include "ui/WebcamPanel.h"

#include "AppSettings.h"
#include "LayoutDialog.h"
#include "SettingsStore.h"
#include "Theme.h"
#include "WebcamController.h"
#include "widgets/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace luma::app {
namespace {

QString twoDecimals(int v)
{
    return QString::number(v / 100.0, 'f', 2);
}

void setChecked(QCheckBox* box, bool on)
{
    const QSignalBlocker block(box);
    box->setChecked(on);
}

} // namespace

WebcamPanel::WebcamPanel(SettingsStore& store, DeviceScanner& scanner, WebcamController& webcam, QWidget* parent)
    : QWidget(parent), m_store(store), m_scanner(scanner), m_webcam(webcam)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 4, 0);
    v->setSpacing(10);

    // Camera ------------------------------------------------------------------
    auto* cam = new CollapsibleSection(QStringLiteral("Camera"), this);
    m_preview = new WebcamPreview(cam);
    m_preview->setMinimumHeight(160);
    cam->body()->addWidget(m_preview);
    m_camera = new QComboBox;
    m_mode = new QComboBox;
    m_mode->setToolTip(QStringLiteral("Only modes the camera reports are listed. 640×480 or 1280×720 at 30 FPS are "
                                      "good low-CPU choices; MJPG modes need CPU to decode."));
    cam->body()->addWidget(ui::row(QStringLiteral("Device"), m_camera, cam));
    cam->body()->addWidget(ui::row(QStringLiteral("Mode"), m_mode, cam));
    m_mirror = new QCheckBox(QStringLiteral("Mirror"), cam);
    cam->body()->addWidget(m_mirror);
    m_state = ui::hint(QString(), cam);
    m_stats = ui::hint(QString(), cam);
    cam->body()->addWidget(m_state);
    cam->body()->addWidget(m_stats);
    v->addWidget(cam);

    // Picture -----------------------------------------------------------------
    auto* pic = new CollapsibleSection(QStringLiteral("Picture"), this);
    auto* tools = new QHBoxLayout;
    auto* autoBtn = new QPushButton(QStringLiteral("Auto adjust"), pic);
    autoBtn->setObjectName("Primary");
    autoBtn->setToolTip(QStringLiteral("Analyses the current image once and sets brightness, contrast and white "
                                       "balance. It does not keep changing afterwards (no pumping)."));
    m_presets = new QComboBox(pic);
    m_presets->addItem(QStringLiteral("Preset..."));
    m_presets->addItems(webcamFilterPresetNames());
    m_presets->setToolTip(QStringLiteral("Starting points you can fine-tune below"));
    auto* reset = new QPushButton(QStringLiteral("Reset"), pic);
    reset->setToolTip(QStringLiteral("Back to the unprocessed camera image"));
    tools->addWidget(autoBtn);
    tools->addWidget(m_presets, 1);
    tools->addWidget(reset);
    pic->body()->addLayout(tools);
    auto* toggles = new QHBoxLayout;
    m_compare = new QCheckBox(QStringLiteral("Before / after"), pic);
    m_compare->setToolTip(QStringLiteral("Show the original and the processed image side by side"));
    m_filtersOn = new QCheckBox(QStringLiteral("Filters on"), pic);
    toggles->addWidget(m_compare);
    toggles->addWidget(m_filtersOn);
    toggles->addStretch();
    pic->body()->addLayout(toggles);
    m_autoInfo = ui::hint(QString(), pic);
    pic->body()->addWidget(m_autoInfo);

    struct Def {
        const char* label;
        float webcam::WebcamFilterSettings::*field;
        float min, max;
        const char* tip;
    };
    const Def defs[] = {
        {"Brightness", &webcam::WebcamFilterSettings::brightness, -0.5f, 0.5f, ""},
        {"Contrast", &webcam::WebcamFilterSettings::contrast, 0.5f, 1.5f, ""},
        {"Saturation", &webcam::WebcamFilterSettings::saturation, 0.f, 2.f, ""},
        {"Gamma", &webcam::WebcamFilterSettings::gamma, 0.5f, 2.5f, "Above 1 lifts shadows without washing out blacks"},
        {"Exposure", &webcam::WebcamFilterSettings::exposure, -2.f, 2.f, "Software exposure compensation (EV)"},
        {"Temperature", &webcam::WebcamFilterSettings::temperature, -1.f, 1.f, "White balance: cool ... warm"},
        {"Tint", &webcam::WebcamFilterSettings::tint, -1.f, 1.f, "Green ... magenta"},
        {"Red", &webcam::WebcamFilterSettings::redGain, 0.5f, 1.5f, "Colour correction"},
        {"Green", &webcam::WebcamFilterSettings::greenGain, 0.5f, 1.5f, "Colour correction"},
        {"Blue", &webcam::WebcamFilterSettings::blueGain, 0.5f, 1.5f, "Colour correction"},
        {"Sharpness", &webcam::WebcamFilterSettings::sharpen, 0.f, 1.f, "Mild values (0.10-0.30) look best on cheap cameras"},
        {"Low-light denoise", &webcam::WebcamFilterSettings::denoise, 0.f, 1.f,
         "Smooths sensor noise where the picture does not move"},
    };
    for (const Def& d : defs) {
        FilterSlider fs;
        fs.field = d.field;
        fs.row = ui::sliderRow(QString::fromLatin1(d.label), static_cast<int>(std::lround(d.min * 100)),
                               static_cast<int>(std::lround(d.max * 100)), pic, twoDecimals, QString::fromLatin1(d.tip));
        pic->body()->addWidget(fs.row.widget);
        const auto field = d.field;
        connect(fs.row.slider, &QSlider::valueChanged, this, [this, field](int value) {
            if (m_updating)
                return;
            editFilters([field, value](webcam::WebcamFilterSettings& f) { f.*field = value / 100.f; });
        });
        m_sliders.push_back(fs);
    }
    m_mono = new QCheckBox(QStringLiteral("Black and white"), pic);
    pic->body()->addWidget(m_mono);
    pic->body()->addWidget(ui::hint(QStringLiteral("Software cannot add detail the sensor never captured: these tools "
                                                   "make a cheap camera look cleaner and better exposed. Sharpness and "
                                                   "denoise cost the most CPU."),
                                    pic));
    v->addWidget(pic);

    // Camera & lighting (hardware controls) --------------------------------------
    auto* hw = new CollapsibleSection(QStringLiteral("Camera && lighting"), this);
    m_controlsBox = new QWidget(hw);
    m_controlsLayout = new QVBoxLayout(m_controlsBox);
    m_controlsLayout->setContentsMargins(0, 0, 0, 0);
    m_controlsLayout->setSpacing(6);
    hw->body()->addWidget(m_controlsBox);
    auto* reread = new QPushButton(QStringLiteral("Read camera controls again"), hw);
    hw->body()->addWidget(reread, 0, Qt::AlignLeft);
    v->addWidget(hw);

    // Green screen --------------------------------------------------------------
    auto* key = new CollapsibleSection(QStringLiteral("Green screen"), this, false);
    m_key = new QCheckBox(QStringLiteral("Remove background colour (chroma key)"), key);
    m_keyColor = new ColorButton;
    m_similarity = ui::sliderRow(QStringLiteral("Similarity"), 0, 100, key, twoDecimals);
    m_smooth = ui::sliderRow(QStringLiteral("Edge softness"), 0, 50, key, twoDecimals);
    m_spill = ui::sliderRow(QStringLiteral("Spill suppression"), 0, 100, key, twoDecimals);
    m_background = new QLineEdit;
    m_background->setReadOnly(true);
    m_background->setPlaceholderText(QStringLiteral("None - the keyed area shows the screen"));
    auto* bgRow = new QWidget(key);
    auto* bgl = new QHBoxLayout(bgRow);
    bgl->setContentsMargins(0, 0, 0, 0);
    auto* bgBrowse = new QPushButton(QStringLiteral("Browse..."), bgRow);
    auto* bgClear = new QPushButton(QStringLiteral("Clear"), bgRow);
    m_background->setParent(bgRow);
    bgl->addWidget(m_background, 1);
    bgl->addWidget(bgBrowse);
    bgl->addWidget(bgClear);
    key->body()->addWidget(m_key);
    key->body()->addWidget(ui::row(QStringLiteral("Key colour"), m_keyColor, key));
    key->body()->addWidget(m_similarity.widget);
    key->body()->addWidget(m_smooth.widget);
    key->body()->addWidget(m_spill.widget);
    key->body()->addWidget(ui::dim(QStringLiteral("Background image"), key));
    key->body()->addWidget(bgRow);
    key->body()->addWidget(ui::hint(QStringLiteral("Needs an evenly lit green (or blue) backdrop. Background blur or "
                                                   "removal without a green screen would need an AI model and is not "
                                                   "included."),
                                    key));
    v->addWidget(key);
    v->addStretch();

    // Handlers -------------------------------------------------------------------
    connect(&m_webcam, &WebcamController::previewFrame, this, [this](const QImage& img) {
        if (m_active)
            m_preview->setFrame(img);
    });
    connect(&m_webcam, &WebcamController::originalFrame, m_preview, &WebcamPreview::setOriginal);
    connect(&m_webcam, &WebcamController::stateChanged, this, [this] {
        m_state->setText(m_webcam.stateText());
        if (!m_webcam.running())
            m_preview->setMessage(m_webcam.stateText());
        else if (!m_controlsLoaded)
            QTimer::singleShot(600, this, &WebcamPanel::rebuildControls);
    });
    connect(&m_webcam, &WebcamController::autoAdjustReady, this, [this](const webcam::FrameStats& st) {
        m_store.edit(SettingsStore::Webcam, [&st](AppSettings& s) {
            s.webcamFilters = webcam::suggestFilters(st, s.webcamFilters);
            s.webcamFilters.enabled = true;
        });
        const auto& f = m_store.get().webcamFilters;
        QString text = QStringLiteral("Auto adjust: scene brightness %1%, highlights %2%. Applied gamma %3, exposure "
                                      "%4 EV, contrast %5, colour balance R %6 / B %7.")
                           .arg(qRound(st.p50 * 100))
                           .arg(qRound(st.p98 * 100))
                           .arg(f.gamma, 0, 'f', 2)
                           .arg(f.exposure, 0, 'f', 2)
                           .arg(f.contrast, 0, 'f', 2)
                           .arg(f.redGain, 0, 'f', 2)
                           .arg(f.blueGain, 0, 'f', 2);
        if (st.p50 < 0.2)
            text += QStringLiteral(" The scene is dark: expect visible noise; more light gives the biggest improvement.");
        m_autoInfo->setText(text);
    });
    connect(&m_scanner, &DeviceScanner::camerasReady, this, &WebcamPanel::fillCameras);
    connect(&m_scanner, &DeviceScanner::cameraModesReady, this, &WebcamPanel::fillModes);

    connect(m_camera, &QComboBox::activated, this, [this](int i) {
        const QString link = m_camera->itemData(i).toString();
        if (link.isEmpty())
            return;
        const QString name = m_camera->itemText(i);
        m_store.edit(SettingsStore::Webcam, [link, name](AppSettings& s) {
            s.webcamLink = link;
            s.webcamName = name;
            s.webcamMode = {};
        });
        m_mode->clear();
        m_mode->addItem(QStringLiteral("Reading camera modes..."));
        m_webcam.stopCamera(); // the device must be free to list its modes (non-blocking)
        m_scanner.scanCameraModes(link);
    });
    connect(m_mode, &QComboBox::activated, this, [this](int i) {
        if (i < 0 || i >= m_modes.size())
            return;
        const CameraModeEntry e = m_modes[i];
        m_store.edit(SettingsStore::Webcam, [e](AppSettings& s) {
            s.webcamMode = {e.width, e.height, e.fpsNum, e.fpsDen, e.format};
        });
    });
    connect(m_mirror, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_store.edit(SettingsStore::Overlays, [on](AppSettings& s) { s.webcamPlacement.mirror = on; });
    });
    connect(autoBtn, &QPushButton::clicked, this, [this] {
        if (!m_webcam.running()) {
            m_autoInfo->setText(QStringLiteral("The camera is not running yet."));
            return;
        }
        m_autoInfo->setText(QStringLiteral("Analysing the camera image..."));
        m_webcam.requestAutoAdjust();
    });
    connect(m_presets, &QComboBox::activated, this, [this](int i) {
        if (i <= 0)
            return;
        const QString name = m_presets->itemText(i);
        editFilters([name](webcam::WebcamFilterSettings& f) {
            applyWebcamFilterPreset(f, name);
            f.enabled = true;
        });
        m_autoInfo->setText(QStringLiteral("Preset \"%1\" applied. Fine-tune it below.").arg(name));
        const QSignalBlocker block(m_presets);
        m_presets->setCurrentIndex(0);
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        editFilters([](webcam::WebcamFilterSettings& f) { applyWebcamFilterPreset(f, QStringLiteral("Natural")); });
        m_autoInfo->setText(QStringLiteral("Picture adjustments reset."));
    });
    connect(m_compare, &QCheckBox::toggled, this, [this](bool on) {
        m_webcam.setCompare(on);
        m_preview->setCompare(on);
    });
    connect(m_filtersOn, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating)
            editFilters([on](webcam::WebcamFilterSettings& f) { f.enabled = on; });
    });
    connect(m_mono, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating)
            editFilters([on](webcam::WebcamFilterSettings& f) { f.monochrome = on; });
    });
    connect(reread, &QPushButton::clicked, this, &WebcamPanel::rebuildControls);
    connect(m_key, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating)
            editFilters([on](webcam::WebcamFilterSettings& f) { f.chromaKey = on; });
    });
    connect(m_keyColor, &ColorButton::colorChanged, this, [this](const QColor& c) {
        editFilters([c](webcam::WebcamFilterSettings& f) { f.keyColor = c.rgb() & 0xFFFFFF; });
    });
    auto keySlider = [this](const ui::SliderRow& r, float webcam::WebcamFilterSettings::*field) {
        connect(r.slider, &QSlider::valueChanged, this, [this, field](int value) {
            if (!m_updating)
                editFilters([field, value](webcam::WebcamFilterSettings& f) { f.*field = value / 100.f; });
        });
    };
    keySlider(m_similarity, &webcam::WebcamFilterSettings::similarity);
    keySlider(m_smooth, &webcam::WebcamFilterSettings::smoothness);
    keySlider(m_spill, &webcam::WebcamFilterSettings::spill);
    connect(bgBrowse, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Background image"), QString(),
                                                          QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (!file.isEmpty())
            m_store.edit(SettingsStore::Webcam, [file](AppSettings& s) { s.webcamBackground = file; });
    });
    connect(bgClear, &QPushButton::clicked, this,
            [this] { m_store.edit(SettingsStore::Webcam, [](AppSettings& s) { s.webcamBackground.clear(); }); });

    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, &WebcamPanel::updateStats);
    connect(&m_store, &SettingsStore::changed, this, [this](unsigned scope) {
        if (scope & (SettingsStore::Webcam | SettingsStore::Overlays))
            refresh();
    });
    fillCameras();
    refresh();
}

void WebcamPanel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    m_webcam.setPreviewRequested(active);
    if (active) {
        m_statsTimer.start();
        if (m_camera->count() <= 1)
            m_scanner.scanCameras();
    } else {
        m_statsTimer.stop();
        m_compare->setChecked(false);
    }
    // MainWindow re-applies the camera (start for configuring / stop when not needed).
    m_store.notify(SettingsStore::Webcam);
}

void WebcamPanel::setBusy(bool busy)
{
    m_busy = busy;
    // The device and mode are opened when the recording starts; picture settings apply live.
    m_camera->setEnabled(!busy && m_camera->count() > 0 && !m_camera->itemData(0).toString().isEmpty());
    m_mode->setEnabled(!busy);
}

void WebcamPanel::editFilters(const std::function<void(webcam::WebcamFilterSettings&)>& change)
{
    m_store.edit(SettingsStore::Webcam, [&change](AppSettings& s) { change(s.webcamFilters); });
}

void WebcamPanel::refresh()
{
    ui::Updating guard(m_updating);
    const AppSettings& s = m_store.get();
    const auto& f = s.webcamFilters;
    for (const FilterSlider& fs : m_sliders)
        fs.row.setValue(static_cast<int>(std::lround(f.*(fs.field) * 100.f)));
    setChecked(m_mono, f.monochrome);
    setChecked(m_filtersOn, f.enabled);
    setChecked(m_mirror, s.webcamPlacement.mirror);
    m_preview->setMirror(s.webcamPlacement.mirror);
    setChecked(m_key, f.chromaKey);
    m_keyColor->setColor(QColor::fromRgb(f.keyColor));
    m_similarity.setValue(static_cast<int>(std::lround(f.similarity * 100)));
    m_smooth.setValue(static_cast<int>(std::lround(f.smoothness * 100)));
    m_spill.setValue(static_cast<int>(std::lround(f.spill * 100)));
    m_background->setText(s.webcamBackground);
    for (const FilterSlider& fs : m_sliders)
        fs.row.widget->setEnabled(f.enabled);
    m_mono->setEnabled(f.enabled);
}

void WebcamPanel::fillCameras()
{
    const QSignalBlocker block(m_camera);
    m_camera->clear();
    const auto& cams = m_scanner.cameras();
    if (cams.isEmpty()) {
        m_camera->addItem(QStringLiteral("No camera found"), QString());
        m_camera->setEnabled(false);
        m_mode->clear();
        m_preview->setMessage(QStringLiteral("No camera found. Connect a webcam; it appears here automatically "
                                             "after Refresh (Sources)."));
        return;
    }
    m_camera->setEnabled(!m_busy);
    for (const CameraEntry& c : cams)
        m_camera->addItem(c.name, c.link);
    const AppSettings& s = m_store.get();
    int idx = m_camera->findData(s.webcamLink);
    if (idx < 0) {
        idx = 0;
        const QString link = cams.first().link, name = cams.first().name;
        m_store.edit(SettingsStore::Webcam, [link, name](AppSettings& st) {
            st.webcamLink = link;
            st.webcamName = name;
        });
    }
    m_camera->setCurrentIndex(idx);
    if (m_busy) {
        m_mode->clear();
        m_mode->addItem(QStringLiteral("%1 × %2 (in use)").arg(s.webcamMode.width).arg(s.webcamMode.height));
        return;
    }
    // Modes are read by opening the device; stop our own capture first so it is free.
    m_webcam.stopCamera();
    m_scanner.scanCameraModes(m_store.get().webcamLink);
}

void WebcamPanel::fillModes(const QString& link, const QList<CameraModeEntry>& modes, const QString& error)
{
    if (link != m_store.get().webcamLink)
        return;
    m_modes = modes;
    const QSignalBlocker block(m_mode);
    m_mode->clear();
    if (!error.isEmpty() || modes.isEmpty()) {
        m_mode->addItem(error.isEmpty() ? QStringLiteral("No modes reported") : QStringLiteral("Unavailable"));
        m_state->setText(error);
        m_store.notify(SettingsStore::Webcam); // restart the camera with its default mode
        return;
    }
    const WebcamModeSetting cur = m_store.get().webcamMode;
    int select = -1;
    for (int i = 0; i < modes.size(); ++i) {
        const CameraModeEntry& e = modes[i];
        m_mode->addItem(e.label());
        if (e.width == cur.width && e.height == cur.height && e.fpsNum * cur.fpsDen == cur.fpsNum * e.fpsDen)
            select = i;
    }
    if (select < 0) {
        // Default: 1280x720 (or the largest <= 720p) at 25-30 fps, else the first mode.
        for (int i = 0; i < modes.size() && select < 0; ++i)
            if (modes[i].height <= 720 && modes[i].fpsDen && modes[i].fpsNum / modes[i].fpsDen >= 25 &&
                modes[i].fpsNum / modes[i].fpsDen <= 30)
                select = i;
        if (select < 0)
            select = 0;
    }
    m_mode->setCurrentIndex(select);
    const CameraModeEntry e = modes[select];
    m_store.edit(SettingsStore::Webcam,
                 [e](AppSettings& s) { s.webcamMode = {e.width, e.height, e.fpsNum, e.fpsDen, e.format}; });
}

void WebcamPanel::rebuildControls()
{
    while (QLayoutItem* item = m_controlsLayout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    const auto controls = m_webcam.controls();
    if (controls.empty()) {
        m_controlsLoaded = false;
        m_controlsLayout->addWidget(ui::hint(m_webcam.running()
                                                 ? QStringLiteral("This camera reports no adjustable hardware controls.")
                                                 : QStringLiteral("The camera's own controls appear here once it runs."),
                                             m_controlsBox));
        return;
    }
    m_controlsLoaded = true;
    using Kind = webcam::CameraControl::Kind;
    bool flicker = false, lowLight = false;
    // Lighting-related controls first, with plain-language choices.
    for (const webcam::CameraControl& c : controls) {
        if (c.kind == Kind::PowerLine) {
            flicker = true;
            auto* combo = new QComboBox;
            combo->addItems({QStringLiteral("Off"), QStringLiteral("50 Hz (Europe, Asia, Africa, Australia)"),
                             QStringLiteral("60 Hz (Americas, parts of Japan)")});
            combo->setCurrentIndex(std::clamp(static_cast<int>(c.value), 0, 2));
            connect(combo, &QComboBox::activated, this, [this, c](int i) {
                webcam::CameraControl n = c;
                n.value = i;
                m_webcam.setControl(n);
            });
            m_controlsLayout->addWidget(ui::row(QStringLiteral("Anti-flicker"), combo, m_controlsBox,
                                                QStringLiteral("Match your mains frequency so the camera's auto exposure "
                                                               "avoids flicker and rolling bands under electric lights")));
        } else if (c.kind == Kind::ExposurePriority) {
            lowLight = true;
            auto* combo = new QComboBox;
            combo->addItems({QStringLiteral("Smooth motion (full frame rate, darker)"),
                             QStringLiteral("Brighter image (may blur / stutter)")});
            combo->setCurrentIndex(c.value ? 1 : 0);
            connect(combo, &QComboBox::activated, this, [this, c](int i) {
                webcam::CameraControl n = c;
                n.value = i;
                m_webcam.setControl(n);
            });
            m_controlsLayout->addWidget(ui::row(QStringLiteral("Low light"), combo, m_controlsBox,
                                                QStringLiteral("In dim rooms many webcams expose longer than one frame. "
                                                               "That brightens the picture but smears motion and repeats "
                                                               "frames. Software cannot undo that; more light helps most.")));
        }
    }
    if (!flicker)
        m_controlsLayout->addWidget(ui::hint(QStringLiteral("Anti-flicker: not supported by this camera."), m_controlsBox));
    if (!lowLight)
        m_controlsLayout->addWidget(ui::hint(QStringLiteral("Low-light frame-rate priority: not supported by this camera."),
                                             m_controlsBox));
    for (const webcam::CameraControl& c : controls) {
        if (c.kind == Kind::PowerLine || c.kind == Kind::ExposurePriority)
            continue;
        auto* row = new QWidget(m_controlsBox);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        QString label = QString::fromStdString(c.name);
        if (c.kind == Kind::Camera && c.name == "Exposure")
            label = QStringLiteral("Exposure (2^n s)");
        auto* l = ui::dim(label, row);
        l->setMinimumWidth(96);
        auto* s = new QSlider(Qt::Horizontal, row);
        s->setRange(static_cast<int>(c.min), static_cast<int>(c.max));
        s->setSingleStep(static_cast<int>(std::max(1L, c.step)));
        s->setPageStep(static_cast<int>(std::max(1L, (c.max - c.min) / 10)));
        s->setValue(static_cast<int>(c.value));
        s->setAccessibleName(label);
        auto* value = new QLabel(QString::number(c.value), row);
        value->setMinimumWidth(36);
        h->addWidget(l);
        h->addWidget(s, 1);
        h->addWidget(value);
        QCheckBox* autoBox = nullptr;
        if (c.autoSupported) {
            autoBox = new QCheckBox(QStringLiteral("Auto"), row);
            autoBox->setChecked(c.autoEnabled);
            s->setEnabled(!c.autoEnabled);
            h->addWidget(autoBox);
        }
        auto send = [this, c, s, autoBox, value] {
            webcam::CameraControl n = c;
            n.value = s->value();
            n.autoEnabled = autoBox && autoBox->isChecked();
            s->setEnabled(!n.autoEnabled);
            value->setText(QString::number(n.value));
            m_webcam.setControl(n);
        };
        connect(s, &QSlider::valueChanged, this, send);
        if (autoBox)
            connect(autoBox, &QCheckBox::toggled, this, send);
        auto* def = new QToolButton(row);
        def->setText(QStringLiteral("Default"));
        def->setToolTip(QStringLiteral("Camera default: %1").arg(c.defaultValue));
        connect(def, &QToolButton::clicked, this, [s, c] { s->setValue(static_cast<int>(c.defaultValue)); });
        h->addWidget(def);
        m_controlsLayout->addWidget(row);
    }
}

void WebcamPanel::updateStats()
{
    const auto st = m_webcam.stats();
    if (!m_webcam.running() || st.fps <= 0) {
        m_stats->clear();
        return;
    }
    const WebcamModeSetting mode = m_store.get().webcamMode;
    const double nominal = mode.fpsNum && mode.fpsDen ? static_cast<double>(mode.fpsNum) / mode.fpsDen : 30.0;
    QString text = QStringLiteral("%1 FPS delivered, longest gap %2 ms, processing %3 ms per frame.")
                       .arg(st.fps, 0, 'f', 0)
                       .arg(st.maxGapMs, 0, 'f', 0)
                       .arg(st.processMs, 0, 'f', 1);
    if (st.maxGapMs > 1800.0 / nominal)
        text += QStringLiteral(" Frames are being held: in dim light the camera exposes longer than one frame, which "
                               "causes blur and stutter. More light helps most; \"Smooth motion\" under Camera & "
                               "lighting keeps the frame rate (darker).");
    m_stats->setText(text);
}

} // namespace luma::app
