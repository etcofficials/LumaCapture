#include "SettingsDialog.h"

#include "AppPaths.h"
#include "AudioMonitor.h"
#include "HotkeyManager.h"
#include "LayoutDialog.h"
#include "WebcamController.h"
#include "WinUtil.h"
#include "webcam/WebcamFilters.h"
#include "widgets/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace luma::app {
namespace {

QLabel* note(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName("Dim");
    l->setWordWrap(true);
    return l;
}

QColor rgbaToColor(const gpu::Rgba& c) { return QColor::fromRgbF(c.r, c.g, c.b, c.a); }
gpu::Rgba colorToRgba(const QColor& c)
{
    return {static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF()),
            static_cast<float>(c.alphaF())};
}

} // namespace

SettingsDialog::SettingsDialog(const AppSettings& settings, DeviceScanner& scanner, WebcamController& webcam,
                               bool recording, QWidget* parent)
    : QDialog(parent), m_s(settings), m_original(settings), m_scanner(scanner), m_webcam(webcam), m_recording(recording)
{
    setWindowTitle(QStringLiteral("LumaCapture settings"));
    resize(1120, 740);
    setMinimumSize(980, 620);
    m_monitor = new AudioMonitor(this);

    auto* root = new QVBoxLayout(this);
    auto* body = new QHBoxLayout;
    m_nav = new QListWidget(this);
    m_nav->setObjectName("Nav");
    m_nav->setFixedWidth(200);
    m_nav->addItems({QStringLiteral("General & output"), QStringLiteral("Screen capture"),
                     QStringLiteral("Video & quality"), QStringLiteral("Audio"), QStringLiteral("Webcam & filters"),
                     QStringLiteral("Hotkeys"), QStringLiteral("Advanced"), QStringLiteral("About & licenses")});
    m_nav->setAccessibleName(QStringLiteral("Settings pages"));
    body->addWidget(m_nav);
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildGeneral());
    m_stack->addWidget(buildScreen());
    m_stack->addWidget(buildVideo());
    m_stack->addWidget(buildAudio());
    m_stack->addWidget(buildWebcam());
    m_stack->addWidget(buildHotkeys());
    m_stack->addWidget(buildAdvanced());
    m_stack->addWidget(buildAbout());
    body->addWidget(m_stack, 1);
    root->addLayout(body, 1);
    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int row) {
        // Keep the camera running for live filter preview only while the webcam page is visible.
        m_webcam.setPreviewRequested(row == Webcam);
        if (!m_recording)
            m_webcam.apply(m_s);
        if (row != Audio)
            m_monitor->stop();
    });
    m_nav->setCurrentRow(0);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::apply);
    connect(this, &QDialog::rejected, this, [this] { m_webcam.apply(m_original); }); // undo live preview changes

    connect(&m_scanner, &DeviceScanner::audioReady, this, &SettingsDialog::fillAudioDevices);
    connect(&m_scanner, &DeviceScanner::camerasReady, this, &SettingsDialog::fillCameras);
    connect(&m_scanner, &DeviceScanner::cameraModesReady, this, &SettingsDialog::fillCameraModes);
    m_scanner.scanAudio();
    m_scanner.scanCameras();
}

SettingsDialog::~SettingsDialog()
{
    m_monitor->stop();
    m_webcam.setPreviewRequested(false);
}

void SettingsDialog::showPage(Page p)
{
    m_nav->setCurrentRow(static_cast<int>(p));
}

void SettingsDialog::apply()
{
    for (int i = 0; i < static_cast<int>(HotkeyAction::Count); ++i)
        if (m_hotkeyEdits[i])
            m_s.hotkeys[i] = m_hotkeyEdits[i]->keySequence();
    m_original = m_s;
    emit applied(m_s);
}

QWidget* SettingsDialog::page(const QString& title, QVBoxLayout*& content)
{
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* w = new QWidget(scroll);
    content = new QVBoxLayout(w);
    content->setContentsMargins(16, 8, 16, 16);
    content->setSpacing(10);
    auto* t = new QLabel(title, w);
    t->setObjectName("Brand");
    content->addWidget(t);
    scroll->setWidget(w);
    return scroll;
}

QSlider* SettingsDialog::addSlider(QFormLayout* form, const QString& label, float* field, float min, float max,
                                   float scale, const QString& tooltip, std::function<void()> onChange)
{
    auto* row = new QWidget(form->parentWidget());
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto* s = new QSlider(Qt::Horizontal, row);
    s->setRange(static_cast<int>(std::lround(min * scale)), static_cast<int>(std::lround(max * scale)));
    s->setValue(static_cast<int>(std::lround(*field * scale)));
    s->setToolTip(tooltip);
    s->setAccessibleName(label);
    auto* value = new QLabel(QString::number(*field, 'f', 2), row);
    value->setMinimumWidth(44);
    h->addWidget(s, 1);
    h->addWidget(value);
    connect(s, &QSlider::valueChanged, this, [field, scale, value, onChange](int v) {
        *field = static_cast<float>(v) / scale;
        value->setText(QString::number(*field, 'f', 2));
        if (onChange)
            onChange();
    });
    form->addRow(label, row);
    return s;
}

// ------------------------------------------------------------------ General

QWidget* SettingsDialog::buildGeneral()
{
    QVBoxLayout* v = nullptr;
    QWidget* p = page(QStringLiteral("General & output"), v);
    auto* box = new QGroupBox(QStringLiteral("Output"), p);
    auto* form = new QFormLayout(box);

    auto folderRow = [this, box](QString* target, const QString& title) {
        auto* w = new QWidget(box);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        auto* edit = new QLineEdit(QDir::toNativeSeparators(*target), w);
        auto* browse = new QPushButton(QStringLiteral("Browse..."), w);
        h->addWidget(edit, 1);
        h->addWidget(browse);
        connect(edit, &QLineEdit::textChanged, this, [target](const QString& t) { *target = QDir::fromNativeSeparators(t); });
        connect(browse, &QPushButton::clicked, this, [this, edit, title] {
            const QString d = QFileDialog::getExistingDirectory(this, title, edit->text());
            if (!d.isEmpty())
                edit->setText(QDir::toNativeSeparators(d));
        });
        return w;
    };
    form->addRow(QStringLiteral("Recordings folder"), folderRow(&m_s.outputDir, QStringLiteral("Recordings folder")));
    form->addRow(QStringLiteral("Screenshots folder"),
                 folderRow(&m_s.screenshotDir, QStringLiteral("Screenshots folder")));

    auto* pattern = new QLineEdit(m_s.namePattern, box);
    pattern->setToolTip(QStringLiteral("Tokens: {date} {time} {source} {res} {fps}"));
    connect(pattern, &QLineEdit::textChanged, this, [this](const QString& t) { m_s.namePattern = t; });
    form->addRow(QStringLiteral("File name pattern"), pattern);
    form->addRow(note(QStringLiteral("Tokens: {date} {time} {source} {res} {fps}. Existing files are never "
                                     "overwritten (a number is appended)."),
                      box));

    auto* container = new QComboBox(box);
    container->addItems({QStringLiteral("MKV (recommended, survives crashes)"),
                         QStringLiteral("MP4 (recorded as MKV, converted after stopping)")});
    container->setCurrentIndex(static_cast<int>(m_s.container));
    connect(container, &QComboBox::currentIndexChanged, this,
            [this](int i) { m_s.container = static_cast<Container>(i); });
    form->addRow(QStringLiteral("Container"), container);
    auto* keep = new QCheckBox(QStringLiteral("Keep the MKV after converting to MP4"), box);
    keep->setChecked(m_s.keepMkvAfterMp4);
    connect(keep, &QCheckBox::toggled, this, [this](bool on) { m_s.keepMkvAfterMp4 = on; });
    form->addRow(keep);

    auto* warn = new QSpinBox(box);
    warn->setRange(100, 100000);
    warn->setSuffix(QStringLiteral(" MB"));
    warn->setValue(m_s.lowSpaceWarnMB);
    warn->setToolTip(QStringLiteral("Warn before recording when the drive has less free space than this"));
    connect(warn, &QSpinBox::valueChanged, this, [this](int v) { m_s.lowSpaceWarnMB = v; });
    form->addRow(QStringLiteral("Low disk space warning"), warn);
    v->addWidget(box);

    auto* ui = new QGroupBox(QStringLiteral("Behaviour && appearance"), p);
    auto* f2 = new QFormLayout(ui);
    auto* theme = new QComboBox(ui);
    theme->addItems({QStringLiteral("Dark"), QStringLiteral("Light")});
    theme->setCurrentIndex(static_cast<int>(m_s.theme));
    connect(theme, &QComboBox::currentIndexChanged, this, [this](int i) { m_s.theme = static_cast<Theme>(i); });
    f2->addRow(QStringLiteral("Theme"), theme);
    auto* countdown = new QSpinBox(ui);
    countdown->setRange(0, 10);
    countdown->setSuffix(QStringLiteral(" s"));
    countdown->setSpecialValueText(QStringLiteral("Off"));
    countdown->setValue(m_s.countdownSeconds);
    connect(countdown, &QSpinBox::valueChanged, this, [this](int v) { m_s.countdownSeconds = v; });
    f2->addRow(QStringLiteral("Countdown before recording"), countdown);
    auto check = [this, ui, f2](const QString& text, bool* field, const QString& tip) {
        auto* c = new QCheckBox(text, ui);
        c->setChecked(*field);
        c->setToolTip(tip);
        connect(c, &QCheckBox::toggled, this, [field](bool on) { *field = on; });
        f2->addRow(c);
    };
    check(QStringLiteral("Minimize LumaCapture when recording starts"), &m_s.minimizeOnRecord, {});
    check(QStringLiteral("Show the floating recording bar"), &m_s.showRecordingBar,
          QStringLiteral("Small always-on-top timer with pause/stop; it is never recorded"));
    check(QStringLiteral("Minimize to the notification area (tray)"), &m_s.minimizeToTray, {});
    v->addWidget(ui);
    v->addStretch();
    return p;
}

// ------------------------------------------------------------------ Screen

QWidget* SettingsDialog::buildScreen()
{
    QVBoxLayout* v = nullptr;
    QWidget* p = page(QStringLiteral("Screen capture"), v);
    auto* box = new QGroupBox(QStringLiteral("Cursor"), p);
    auto* form = new QFormLayout(box);
    auto* cursor = new QCheckBox(QStringLiteral("Record the mouse cursor"), box);
    cursor->setChecked(m_s.captureCursor);
    connect(cursor, &QCheckBox::toggled, this, [this](bool on) { m_s.captureCursor = on; });
    form->addRow(cursor);

    auto* hl = new QCheckBox(QStringLiteral("Highlight the cursor"), box);
    hl->setChecked(m_s.cursor.highlight);
    connect(hl, &QCheckBox::toggled, this, [this](bool on) { m_s.cursor.highlight = on; });
    auto* hlColor = new ColorButton(box);
    hlColor->setColor(rgbaToColor(m_s.cursor.highlightColor));
    connect(hlColor, &ColorButton::colorChanged, this, [this](const QColor& c) { m_s.cursor.highlightColor = colorToRgba(c); });
    auto* hlRow = new QHBoxLayout;
    hlRow->addWidget(hl);
    hlRow->addWidget(hlColor);
    hlRow->addStretch();
    form->addRow(hlRow);
    addSlider(form, QStringLiteral("Highlight radius"), &m_s.cursor.highlightRadius, 10, 80, 1,
              QStringLiteral("In screen pixels"), nullptr);

    auto* clicks = new QCheckBox(QStringLiteral("Show mouse clicks (left / right in different colours)"), box);
    clicks->setChecked(m_s.cursor.clicks);
    connect(clicks, &QCheckBox::toggled, this, [this](bool on) { m_s.cursor.clicks = on; });
    form->addRow(clicks);
    auto* left = new ColorButton(box);
    left->setColor(rgbaToColor(m_s.cursor.leftClickColor));
    connect(left, &ColorButton::colorChanged, this, [this](const QColor& c) { m_s.cursor.leftClickColor = colorToRgba(c); });
    auto* right = new ColorButton(box);
    right->setColor(rgbaToColor(m_s.cursor.rightClickColor));
    connect(right, &ColorButton::colorChanged, this, [this](const QColor& c) { m_s.cursor.rightClickColor = colorToRgba(c); });
    auto* clickRow = new QHBoxLayout;
    clickRow->addWidget(new QLabel(QStringLiteral("Left"), box));
    clickRow->addWidget(left);
    clickRow->addWidget(new QLabel(QStringLiteral("Right"), box));
    clickRow->addWidget(right);
    clickRow->addStretch();
    form->addRow(QStringLiteral("Click colours"), clickRow);
    addSlider(form, QStringLiteral("Click ring size"), &m_s.cursor.clickRadius, 10, 80, 1, {}, nullptr);
    form->addRow(note(QStringLiteral("Cursor effects need \"Record the mouse cursor\". Clicks are detected by "
                                     "polling the mouse buttons each frame, so extremely short taps can be missed."),
                      box));
    v->addWidget(box);

    auto* own = new QGroupBox(QStringLiteral("LumaCapture windows"), p);
    auto* f2 = new QFormLayout(own);
    auto* excl = new QCheckBox(QStringLiteral("Hide LumaCapture's own windows from recordings and screenshots"), own);
    excl->setChecked(m_s.excludeOwnWindows);
    connect(excl, &QCheckBox::toggled, this, [this](bool on) { m_s.excludeOwnWindows = on; });
    f2->addRow(excl);
    f2->addRow(note(QStringLiteral("Uses the Windows capture-exclusion flag (Windows 10 version 2004 or newer). "
                                   "Turn it off if you want to record the LumaCapture window itself."),
                    own));
    v->addWidget(own);

    auto* crop = new QGroupBox(QStringLiteral("Crop (display and window capture)"), p);
    auto* f3 = new QFormLayout(crop);
    auto cropSpin = [this, crop, f3](const QString& label, int* field) {
        auto* s = new QSpinBox(crop);
        s->setRange(0, 4000);
        s->setSuffix(QStringLiteral(" px"));
        s->setValue(*field);
        connect(s, &QSpinBox::valueChanged, this, [field](int v) { *field = v; });
        f3->addRow(label, s);
    };
    cropSpin(QStringLiteral("Left"), &m_s.cropLeft);
    cropSpin(QStringLiteral("Top"), &m_s.cropTop);
    cropSpin(QStringLiteral("Right"), &m_s.cropRight);
    cropSpin(QStringLiteral("Bottom"), &m_s.cropBottom);
    v->addWidget(crop);

    v->addWidget(note(QStringLiteral("Window capture uses Windows Graphics Capture. On Windows 10 a yellow border "
                                     "is drawn around the captured window by Windows itself; it cannot be turned off "
                                     "before Windows 11. A minimized window keeps showing its last image."),
                      p));
    v->addStretch();
    return p;
}

// ------------------------------------------------------------------- Video

QWidget* SettingsDialog::buildVideo()
{
    QVBoxLayout* v = nullptr;
    QWidget* p = page(QStringLiteral("Video & quality"), v);
    auto* box = new QGroupBox(QStringLiteral("Encoding (H.264, software x264)"), p);
    auto* form = new QFormLayout(box);

    auto* res = new QComboBox(box);
    res->addItems({QStringLiteral("Native (source size)"), QStringLiteral("1080p (1920x1080)"),
                   QStringLiteral("900p (1600x900)"), QStringLiteral("720p (1280x720)"),
                   QStringLiteral("480p (854x480)")});
    res->setCurrentIndex(static_cast<int>(m_s.resolution));
    res->setToolTip(QStringLiteral("Target height; the source aspect ratio is kept and sources are never upscaled"));
    connect(res, &QComboBox::currentIndexChanged, this, [this](int i) { m_s.resolution = static_cast<ResolutionPreset>(i); });
    form->addRow(QStringLiteral("Resolution"), res);

    auto* fps = new QComboBox(box);
    fps->addItems({QStringLiteral("24"), QStringLiteral("30"), QStringLiteral("60")});
    fps->setCurrentText(QString::number(m_s.fps));
    connect(fps, &QComboBox::currentTextChanged, this, [this](const QString& t) { m_s.fps = t.toInt(); });
    form->addRow(QStringLiteral("Frame rate"), fps);

    auto* quality = new QComboBox(box);
    quality->addItems({QStringLiteral("Small file (CRF 28)"), QStringLiteral("Balanced (CRF 23)"),
                       QStringLiteral("High quality (CRF 19)"), QStringLiteral("Custom CRF")});
    quality->setCurrentIndex(static_cast<int>(m_s.quality));
    auto* crf = new QSpinBox(box);
    crf->setRange(10, 40);
    crf->setValue(m_s.crf);
    crf->setToolTip(QStringLiteral("Lower = better quality and bigger files. 18-28 is the useful range."));
    crf->setEnabled(m_s.quality == QualityPreset::Custom);
    connect(quality, &QComboBox::currentIndexChanged, this, [this, crf](int i) {
        m_s.quality = static_cast<QualityPreset>(i);
        crf->setEnabled(i == static_cast<int>(QualityPreset::Custom));
    });
    connect(crf, &QSpinBox::valueChanged, this, [this](int v) { m_s.crf = v; });
    form->addRow(QStringLiteral("Quality"), quality);
    form->addRow(QStringLiteral("CRF"), crf);

    auto* preset = new QComboBox(box);
    preset->addItems({QStringLiteral("ultrafast"), QStringLiteral("superfast"), QStringLiteral("veryfast"),
                      QStringLiteral("faster")});
    preset->setCurrentText(m_s.x264Preset);
    preset->setToolTip(QStringLiteral("Encoder speed. Slower presets compress better but need much more CPU."));
    connect(preset, &QComboBox::currentTextChanged, this, [this](const QString& t) { m_s.x264Preset = t; });
    form->addRow(QStringLiteral("Encoder speed (x264 preset)"), preset);

    auto* key = new QDoubleSpinBox(box);
    key->setRange(0.5, 10);
    key->setSingleStep(0.5);
    key->setSuffix(QStringLiteral(" s"));
    key->setValue(m_s.keyframeSeconds);
    connect(key, &QDoubleSpinBox::valueChanged, this, [this](double d) { m_s.keyframeSeconds = d; });
    form->addRow(QStringLiteral("Keyframe interval"), key);

    auto* threads = new QSpinBox(box);
    threads->setRange(1, 8);
    threads->setValue(m_s.encoderThreads);
    threads->setToolTip(QStringLiteral("3 leaves one of the four cores for capture, audio and the UI"));
    connect(threads, &QSpinBox::valueChanged, this, [this](int n) { m_s.encoderThreads = n; });
    form->addRow(QStringLiteral("Encoder threads"), threads);
    v->addWidget(box);

    auto* warn = new QLabel(QStringLiteral(
        "Measured on this PC with a moving full-screen video: 1080p30 and 720p30/60 with \"ultrafast\" kept up; "
        "1080p60 and slower presets at 1080p dropped frames because the i5-2400S throttles under sustained load. "
        "Results depend on what you record and on CPU temperature. There is no hardware encoder on the GT 730."),
        p);
    warn->setObjectName("Warn");
    warn->setWordWrap(true);
    v->addWidget(warn);

    auto* fx = new QGroupBox(QStringLiteral("Screen colour filters (GPU, applied while recording)"), p);
    auto* f2 = new QFormLayout(fx);
    auto* en = new QCheckBox(QStringLiteral("Enable screen filters"), fx);
    en->setChecked(m_s.videoFilters.enabled);
    connect(en, &QCheckBox::toggled, this, [this](bool on) { m_s.videoFilters.enabled = on; });
    f2->addRow(en);
    auto& vf = m_s.videoFilters;
    addSlider(f2, QStringLiteral("Brightness"), &vf.brightness, -0.5f, 0.5f, 100, {}, nullptr);
    addSlider(f2, QStringLiteral("Contrast"), &vf.contrast, 0.5f, 1.5f, 100, {}, nullptr);
    addSlider(f2, QStringLiteral("Saturation"), &vf.saturation, 0.f, 2.f, 100, {}, nullptr);
    addSlider(f2, QStringLiteral("Gamma"), &vf.gamma, 0.5f, 2.f, 100, {}, nullptr);
    addSlider(f2, QStringLiteral("Temperature"), &vf.temperature, -1.f, 1.f, 100, QStringLiteral("Cool ... warm"), nullptr);
    addSlider(f2, QStringLiteral("Tint"), &vf.tint, -1.f, 1.f, 100, QStringLiteral("Green ... magenta"), nullptr);
    addSlider(f2, QStringLiteral("Sharpen"), &vf.sharpen, 0.f, 1.f, 100, {}, nullptr);
    addSlider(f2, QStringLiteral("Blur (privacy)"), &vf.blur, 0.f, 8.f, 10,
              QStringLiteral("Blurs the whole screen image; 0 = off"), nullptr);
    auto* gray = new QCheckBox(QStringLiteral("Grayscale"), fx);
    gray->setChecked(vf.grayscale);
    connect(gray, &QCheckBox::toggled, this, [this](bool on) { m_s.videoFilters.grayscale = on; });
    f2->addRow(gray);
    f2->addRow(note(QStringLiteral("Filters run inside the GPU colour-conversion pass, so they add little load. "
                                   "Changes apply to a running recording immediately."),
                    fx));
    v->addWidget(fx);
    v->addStretch();
    return p;
}

// ------------------------------------------------------------------- Audio

QWidget* SettingsDialog::buildAudio()
{
    QVBoxLayout* v = nullptr;
    QWidget* p = page(QStringLiteral("Audio"), v);

    auto* box = new QGroupBox(QStringLiteral("Sources"), p);
    auto* form = new QFormLayout(box);
    auto* sys = new QCheckBox(QStringLiteral("Record system audio (what you hear)"), box);
    sys->setChecked(m_s.systemAudio);
    connect(sys, &QCheckBox::toggled, this, [this](bool on) { m_s.systemAudio = on; });
    form->addRow(sys);
    m_systemDevice = new QComboBox(box);
    connect(m_systemDevice, &QComboBox::activated, this,
            [this](int i) { m_s.systemDevice = m_systemDevice->itemData(i).toString(); });
    form->addRow(QStringLiteral("Playback device"), m_systemDevice);
    auto* sysVol = new QSlider(Qt::Horizontal, box);
    sysVol->setRange(0, 200);
    sysVol->setValue(static_cast<int>(m_s.systemVolume * 100));
    connect(sysVol, &QSlider::valueChanged, this, [this](int x) { m_s.systemVolume = x / 100.0; });
    form->addRow(QStringLiteral("System volume"), sysVol);

    auto* mic = new QCheckBox(QStringLiteral("Record microphone"), box);
    mic->setChecked(m_s.microphone);
    connect(mic, &QCheckBox::toggled, this, [this](bool on) { m_s.microphone = on; });
    form->addRow(mic);
    m_micDevice = new QComboBox(box);
    connect(m_micDevice, &QComboBox::activated, this,
            [this](int i) { m_s.micDevice = m_micDevice->itemData(i).toString(); });
    form->addRow(QStringLiteral("Microphone"), m_micDevice);
    auto* micVol = new QSlider(Qt::Horizontal, box);
    micVol->setRange(0, 200);
    micVol->setValue(static_cast<int>(m_s.micVolume * 100));
    connect(micVol, &QSlider::valueChanged, this, [this](int x) { m_s.micVolume = x / 100.0; });
    form->addRow(QStringLiteral("Microphone volume"), micVol);
    auto* delay = new QSpinBox(box);
    delay->setRange(-500, 500);
    delay->setSuffix(QStringLiteral(" ms"));
    delay->setValue(m_s.micDelayMs);
    delay->setToolTip(QStringLiteral("Shift the microphone later (positive) or earlier (negative) to fix lip sync"));
    connect(delay, &QSpinBox::valueChanged, this, [this](int x) { m_s.micDelayMs = x; });
    form->addRow(QStringLiteral("Microphone delay"), delay);

    m_sysMeter = new LevelMeter(box);
    m_micMeter = new LevelMeter(box);
    auto* test = new QPushButton(QStringLiteral("Test levels"), box);
    test->setCheckable(true);
    test->setToolTip(QStringLiteral("Opens the selected devices briefly to show their levels (nothing is recorded)"));
    connect(test, &QPushButton::toggled, this, [this](bool on) {
        if (on)
            m_monitor->start(true, m_s.systemDevice, true, m_s.micDevice);
        else
            m_monitor->stop();
    });
    connect(m_monitor, &AudioMonitor::levels, this, [this](float s, float m) {
        m_sysMeter->setLevel(s);
        m_micMeter->setLevel(m);
    });
    form->addRow(QStringLiteral("System level"), m_sysMeter);
    form->addRow(QStringLiteral("Microphone level"), m_micMeter);
    form->addRow(test);
    v->addWidget(box);

    auto* enc = new QGroupBox(QStringLiteral("Encoding"), p);
    auto* fe = new QFormLayout(enc);
    auto* bitrate = new QComboBox(enc);
    bitrate->addItems({QStringLiteral("96"), QStringLiteral("128"), QStringLiteral("160"), QStringLiteral("192"),
                       QStringLiteral("256")});
    bitrate->setCurrentText(QString::number(m_s.audioBitrate));
    connect(bitrate, &QComboBox::currentTextChanged, this, [this](const QString& t) { m_s.audioBitrate = t.toInt(); });
    fe->addRow(QStringLiteral("AAC bitrate (kbit/s)"), bitrate);
    auto* sep = new QCheckBox(QStringLiteral("Also store system audio and microphone as separate tracks"), enc);
    sep->setChecked(m_s.separateTracks);
    sep->setToolTip(QStringLiteral("Track 1 = mix (what players play), track 2 = system, track 3 = microphone"));
    connect(sep, &QCheckBox::toggled, this, [this](bool on) { m_s.separateTracks = on; });
    fe->addRow(sep);
    v->addWidget(enc);

    auto* fx = new QGroupBox(QStringLiteral("Microphone processing"), p);
    auto* ff = new QFormLayout(fx);
    auto& m = m_s.micFilters;
    auto check = [this, fx, ff](const QString& text, bool* field, const QString& tip) {
        auto* c = new QCheckBox(text, fx);
        c->setChecked(*field);
        c->setToolTip(tip);
        connect(c, &QCheckBox::toggled, this, [field](bool on) { *field = on; });
        ff->addRow(c);
    };
    check(QStringLiteral("Enable microphone processing"), &m.enabled, QStringLiteral("Bypasses every stage when off"));
    addSlider(ff, QStringLiteral("Input gain (dB)"), &m.gainDb, -12, 24, 1, {}, nullptr);
    check(QStringLiteral("Rumble filter (high-pass 80 Hz)"), &m.highPass, {});
    check(QStringLiteral("Noise suppression (FFmpeg afftdn)"), &m.noiseSuppression,
          QStringLiteral("FFT denoiser for steady background noise (fans, hiss). Uses some CPU."));
    addSlider(ff, QStringLiteral("Noise reduction (dB)"), &m.noiseReductionDb, 3, 40, 1, {}, nullptr);
    check(QStringLiteral("Noise gate"), &m.gate, QStringLiteral("Silences the microphone below the threshold"));
    addSlider(ff, QStringLiteral("Gate threshold (dB)"), &m.gateThresholdDb, -80, -10, 1, {}, nullptr);
    check(QStringLiteral("Compressor"), &m.compressor, QStringLiteral("Evens out loud and quiet speech"));
    addSlider(ff, QStringLiteral("Compressor threshold (dB)"), &m.compThresholdDb, -50, 0, 1, {}, nullptr);
    addSlider(ff, QStringLiteral("Compressor ratio"), &m.compRatio, 1, 10, 10, {}, nullptr);
    addSlider(ff, QStringLiteral("Make-up gain (dB)"), &m.compMakeupDb, 0, 18, 1, {}, nullptr);
    check(QStringLiteral("Limiter"), &m.limiter, QStringLiteral("Prevents clipping"));
    addSlider(ff, QStringLiteral("Limiter ceiling (dB)"), &m.limiterCeilingDb, -12, 0, 10, {}, nullptr);
    check(QStringLiteral("Equalizer"), &m.eq, {});
    auto* eqPreset = new QComboBox(fx);
    eqPreset->addItems({QStringLiteral("Custom"), QStringLiteral("Flat"), QStringLiteral("Voice clarity"),
                        QStringLiteral("Warm voice"), QStringLiteral("Reduce boominess")});
    ff->addRow(QStringLiteral("EQ preset"), eqPreset);
    QSlider* low = addSlider(ff, QStringLiteral("Low (150 Hz) dB"), &m.eqLowDb, -12, 12, 1, {}, nullptr);
    QSlider* mid = addSlider(ff, QStringLiteral("Mid (2 kHz) dB"), &m.eqMidDb, -12, 12, 1, {}, nullptr);
    QSlider* high = addSlider(ff, QStringLiteral("High (7 kHz) dB"), &m.eqHighDb, -12, 12, 1, {}, nullptr);
    connect(eqPreset, &QComboBox::currentIndexChanged, this, [low, mid, high](int i) {
        const int values[5][3] = {{0, 0, 0}, {0, 0, 0}, {-2, 3, 2}, {3, -1, -2}, {-5, 1, 1}};
        if (i <= 0)
            return;
        low->setValue(values[i][0]);
        mid->setValue(values[i][1]);
        high->setValue(values[i][2]);
    });
    ff->addRow(note(QStringLiteral("Echo cancellation is not available: the capture path has no acoustic echo "
                                   "canceller. Use headphones to avoid speaker sound reaching the microphone."),
                    fx));
    v->addWidget(fx);
    v->addStretch();
    return p;
}

void SettingsDialog::fillAudioDevices()
{
    auto fill = [](QComboBox* combo, const QList<AudioEntry>& list, const QString& current) {
        if (!combo)
            return;
        combo->clear();
        combo->addItem(QStringLiteral("Windows default device"), QString());
        for (const AudioEntry& e : list)
            combo->addItem(e.name + (e.isDefault ? QStringLiteral(" (default)") : QString()), e.id);
        const int idx = combo->findData(current);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
    };
    fill(m_systemDevice, m_scanner.renderDevices(), m_s.systemDevice);
    fill(m_micDevice, m_scanner.captureDevices(), m_s.micDevice);
}

// ------------------------------------------------------------------ Webcam

void SettingsDialog::webcamFiltersChanged()
{
    if (m_recording) {
        // While recording only the filters may change (the camera keeps running).
        AppSettings live = m_original;
        live.webcamFilters = m_s.webcamFilters;
        live.webcamBackground = m_s.webcamBackground;
        m_webcam.apply(live);
        return;
    }
    m_webcam.apply(m_s); // live preview of the processed image
}

void SettingsDialog::syncWebcamSliders()
{
    const auto& f = m_s.webcamFilters;
    const float values[] = {f.brightness, f.contrast, f.saturation, f.gamma,     f.exposure, f.temperature,
                            f.tint,       f.redGain,  f.greenGain,  f.blueGain,  f.sharpen,  f.denoise};
    // Not blocked: valueChanged refreshes each value label (and re-sends the same filters, which is cheap).
    for (size_t k = 0; k < m_webcamSliders.size() && k < std::size(values); ++k)
        m_webcamSliders[k]->setValue(static_cast<int>(std::lround(values[k] * 100.f)));
    if (m_monoBox) {
        QSignalBlocker block(m_monoBox);
        m_monoBox->setChecked(f.monochrome);
    }
    if (m_filtersOn) {
        QSignalBlocker block(m_filtersOn);
        m_filtersOn->setChecked(f.enabled);
    }
}

QWidget* SettingsDialog::buildWebcam()
{
    QVBoxLayout* v = nullptr;
    QWidget* p = page(QStringLiteral("Webcam"), v);

    // ---- Preview + camera selection ------------------------------------------
    auto* top = new QHBoxLayout;
    top->setSpacing(14);
    auto* previewCol = new QVBoxLayout;
    m_preview = new WebcamPreview(p);
    m_preview->setMinimumSize(420, 236);
    m_preview->setMirror(m_s.webcamPlacement.mirror);
    connect(&m_webcam, &WebcamController::previewFrame, m_preview, &WebcamPreview::setFrame);
    connect(&m_webcam, &WebcamController::originalFrame, m_preview, &WebcamPreview::setOriginal);
    previewCol->addWidget(m_preview, 1);

    auto* tools = new QHBoxLayout;
    tools->setSpacing(8);
    auto* autoBtn = new QPushButton(QStringLiteral("Auto adjust"), p);
    autoBtn->setObjectName("Primary");
    autoBtn->setToolTip(QStringLiteral("Analyses the current image once and sets exposure, contrast and white "
                                       "balance. It does not keep changing afterwards (no pumping)."));
    m_presetCombo = new QComboBox(p);
    m_presetCombo->addItem(QStringLiteral("Preset..."));
    m_presetCombo->addItems(webcamFilterPresetNames());
    m_presetCombo->setToolTip(QStringLiteral("Starting points you can fine-tune below"));
    auto* reset = new QPushButton(QStringLiteral("Reset"), p);
    reset->setToolTip(QStringLiteral("Back to the unprocessed camera image"));
    m_compareBox = new QCheckBox(QStringLiteral("Before / after"), p);
    m_compareBox->setToolTip(QStringLiteral("Show the original and the processed image side by side"));
    m_filtersOn = new QCheckBox(QStringLiteral("Filters on"), p);
    m_filtersOn->setChecked(m_s.webcamFilters.enabled);
    tools->addWidget(autoBtn);
    tools->addWidget(m_presetCombo);
    tools->addWidget(reset);
    tools->addStretch();
    tools->addWidget(m_compareBox);
    tools->addWidget(m_filtersOn);
    previewCol->addLayout(tools);
    m_autoInfo = new QLabel(p);
    m_autoInfo->setObjectName("Hint");
    m_autoInfo->setWordWrap(true);
    previewCol->addWidget(m_autoInfo);
    top->addLayout(previewCol, 3);

    auto* box = new QGroupBox(QStringLiteral("Camera"), p);
    auto* form = new QFormLayout(box);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    auto* enabled = new QCheckBox(QStringLiteral("Show webcam in the recording"), box);
    enabled->setChecked(m_s.webcamEnabled);
    connect(enabled, &QCheckBox::toggled, this, [this](bool on) {
        m_s.webcamEnabled = on;
        webcamFiltersChanged();
    });
    form->addRow(enabled);
    m_camera = new QComboBox(box);
    m_camera->setAccessibleName(QStringLiteral("Camera"));
    form->addRow(QStringLiteral("Device"), m_camera);
    m_cameraMode = new QComboBox(box);
    m_cameraMode->setToolTip(QStringLiteral("Only modes the camera reports are listed. 640x480 or 1280x720 at "
                                            "30 fps are good low-CPU choices; MJPG modes need CPU to decode."));
    form->addRow(QStringLiteral("Resolution and frame rate"), m_cameraMode);
    auto* preview = new QCheckBox(QStringLiteral("Live preview in the main window"), box);
    preview->setChecked(m_s.webcamPreview);
    preview->setToolTip(QStringLiteral("Turning the preview off saves a little CPU; the webcam is still recorded"));
    connect(preview, &QCheckBox::toggled, this, [this](bool on) { m_s.webcamPreview = on; });
    form->addRow(preview);
    m_cameraState = note(QString(), box);
    form->addRow(m_cameraState);
    m_statsLabel = note(QString(), box);
    form->addRow(m_statsLabel);
    if (m_recording) {
        m_camera->setEnabled(false);
        m_cameraMode->setEnabled(false);
        form->addRow(note(QStringLiteral("Device and mode can be changed after the recording stops. "
                                         "Image settings apply live."),
                          box));
    }
    box->setMinimumWidth(300);
    box->setMaximumWidth(380);
    top->addWidget(box, 2);
    v->addLayout(top);

    connect(m_camera, &QComboBox::activated, this, [this](int i) {
        const QString link = m_camera->itemData(i).toString();
        m_s.webcamLink = link;
        m_s.webcamName = m_camera->itemText(i);
        m_s.webcamMode = {};
        m_cameraMode->clear();
        m_cameraMode->addItem(QStringLiteral("Reading camera modes..."));
        m_webcam.stopCamera(); // the device must be free to list its modes (non-blocking)
        m_scanner.scanCameraModes(link);
    });
    connect(m_cameraMode, &QComboBox::activated, this, [this](int i) {
        if (i < 0 || i >= m_modes.size())
            return;
        const CameraModeEntry& e = m_modes[i];
        m_s.webcamMode = {e.width, e.height, e.fpsNum, e.fpsDen, e.format};
        webcamFiltersChanged();
    });
    connect(&m_webcam, &WebcamController::stateChanged, this, [this] {
        m_cameraState->setText(m_webcam.stateText());
        if (!m_webcam.running())
            m_preview->setMessage(m_webcam.stateText());
        else if (!m_controlsLoaded)
            QTimer::singleShot(600, this, &SettingsDialog::rebuildCameraControls);
    });

    // Live pacing diagnostics (explains the "shutter" / stutter effect when it happens).
    auto* statsTimer = new QTimer(this);
    statsTimer->setInterval(1000);
    connect(statsTimer, &QTimer::timeout, this, [this] {
        const auto st = m_webcam.stats();
        if (!m_webcam.running() || st.fps <= 0) {
            m_statsLabel->clear();
            return;
        }
        const double nominal = m_s.webcamMode.fpsNum && m_s.webcamMode.fpsDen
                                   ? static_cast<double>(m_s.webcamMode.fpsNum) / m_s.webcamMode.fpsDen
                                   : 30.0;
        QString text = QStringLiteral("Delivering %1 fps, longest gap %2 ms, image processing %3 ms per frame.")
                           .arg(st.fps, 0, 'f', 0)
                           .arg(st.maxGapMs, 0, 'f', 0)
                           .arg(st.processMs, 0, 'f', 1);
        if (st.maxGapMs > 1800.0 / nominal)
            text += QStringLiteral(" Some frames are held for two frame times: in dim light the camera exposes "
                                   "longer than one frame, which causes motion blur and stutter. More light helps "
                                   "most; \"Smooth motion\" under Camera & lighting keeps the frame rate (darker).");
        m_statsLabel->setText(text);
    });
    statsTimer->start();

    // ---- Tabs ----------------------------------------------------------------
    auto* tabs = new QTabWidget(p);

    // Image
    auto* imageTab = new QWidget(tabs);
    auto* ff = new QFormLayout(imageTab);
    ff->setContentsMargins(14, 14, 14, 14);
    auto& f = m_s.webcamFilters;
    auto cb = [this] { webcamFiltersChanged(); };
    m_webcamSliders.clear();
    m_webcamSliders.push_back(addSlider(ff, QStringLiteral("Brightness"), &f.brightness, -0.5f, 0.5f, 100, {}, cb));
    m_webcamSliders.push_back(addSlider(ff, QStringLiteral("Contrast"), &f.contrast, 0.5f, 1.5f, 100, {}, cb));
    m_webcamSliders.push_back(addSlider(ff, QStringLiteral("Saturation"), &f.saturation, 0.f, 2.f, 100, {}, cb));
    m_webcamSliders.push_back(addSlider(ff, QStringLiteral("Gamma"), &f.gamma, 0.5f, 2.5f, 100,
                                        QStringLiteral("Above 1 lifts shadows without washing out blacks"), cb));
    m_webcamSliders.push_back(addSlider(ff, QStringLiteral("Exposure (EV)"), &f.exposure, -2.f, 2.f, 100,
                                        QStringLiteral("Software exposure compensation"), cb));
    m_webcamSliders.push_back(addSlider(ff, QStringLiteral("Temperature"), &f.temperature, -1.f, 1.f, 100,
                                        QStringLiteral("White balance: cool ... warm"), cb));
    m_webcamSliders.push_back(addSlider(ff, QStringLiteral("Tint"), &f.tint, -1.f, 1.f, 100,
                                        QStringLiteral("Green ... magenta"), cb));
    m_webcamSliders.push_back(addSlider(ff, QStringLiteral("Red"), &f.redGain, 0.5f, 1.5f, 100, QStringLiteral("Colour correction"), cb));
    m_webcamSliders.push_back(addSlider(ff, QStringLiteral("Green"), &f.greenGain, 0.5f, 1.5f, 100, QStringLiteral("Colour correction"), cb));
    m_webcamSliders.push_back(addSlider(ff, QStringLiteral("Blue"), &f.blueGain, 0.5f, 1.5f, 100, QStringLiteral("Colour correction"), cb));
    m_webcamSliders.push_back(addSlider(ff, QStringLiteral("Sharpen"), &f.sharpen, 0.f, 1.f, 100,
                                        QStringLiteral("Mild values (0.1-0.3) look best on cheap cameras"), cb));
    m_webcamSliders.push_back(addSlider(ff, QStringLiteral("Low-light denoise"), &f.denoise, 0.f, 1.f, 100,
                                        QStringLiteral("Smooths sensor noise where the picture does not move"), cb));
    m_monoBox = new QCheckBox(QStringLiteral("Monochrome"), imageTab);
    m_monoBox->setChecked(f.monochrome);
    connect(m_monoBox, &QCheckBox::toggled, this, [this](bool on) {
        m_s.webcamFilters.monochrome = on;
        webcamFiltersChanged();
    });
    ff->addRow(m_monoBox);
    ff->addRow(note(QStringLiteral("Software cannot add detail the sensor never captured. These tools make a cheap "
                                   "camera look cleaner and better exposed; sharpen and denoise cost the most CPU."),
                    imageTab));
    auto* imageScroll = new QScrollArea(tabs);
    imageScroll->setWidgetResizable(true);
    imageScroll->setWidget(imageTab);
    tabs->addTab(imageScroll, QStringLiteral("Image"));

    // Camera & lighting (hardware controls, filled when the camera runs)
    auto* hwTab = new QWidget(tabs);
    auto* hv = new QVBoxLayout(hwTab);
    hv->setContentsMargins(14, 14, 14, 14);
    m_controlsBox = new QWidget(hwTab);
    m_controlsForm = new QFormLayout(m_controlsBox);
    m_controlsForm->setContentsMargins(0, 0, 0, 0);
    hv->addWidget(m_controlsBox);
    auto* reread = new QPushButton(QStringLiteral("Read controls again"), hwTab);
    connect(reread, &QPushButton::clicked, this, &SettingsDialog::rebuildCameraControls);
    hv->addWidget(reread, 0, Qt::AlignLeft);
    hv->addStretch();
    auto* hwScroll = new QScrollArea(tabs);
    hwScroll->setWidgetResizable(true);
    hwScroll->setWidget(hwTab);
    tabs->addTab(hwScroll, QStringLiteral("Camera && lighting"));

    // Green screen
    auto* key = new QWidget(tabs);
    auto* fk = new QFormLayout(key);
    fk->setContentsMargins(14, 14, 14, 14);
    auto* keyOn = new QCheckBox(QStringLiteral("Remove background colour (chroma key)"), key);
    keyOn->setChecked(f.chromaKey);
    connect(keyOn, &QCheckBox::toggled, this, [this](bool on) {
        m_s.webcamFilters.chromaKey = on;
        webcamFiltersChanged();
    });
    fk->addRow(keyOn);
    auto* keyColor = new ColorButton(key);
    keyColor->setColor(QColor::fromRgb(f.keyColor));
    connect(keyColor, &ColorButton::colorChanged, this, [this](const QColor& c) {
        m_s.webcamFilters.keyColor = c.rgb() & 0xFFFFFF;
        webcamFiltersChanged();
    });
    fk->addRow(QStringLiteral("Key colour"), keyColor);
    addSlider(fk, QStringLiteral("Similarity"), &f.similarity, 0.f, 1.f, 100, {}, cb);
    addSlider(fk, QStringLiteral("Edge softness"), &f.smoothness, 0.f, 0.5f, 100, {}, cb);
    addSlider(fk, QStringLiteral("Spill suppression"), &f.spill, 0.f, 1.f, 100, {}, cb);
    auto* bgRow = new QHBoxLayout;
    auto* bg = new QLineEdit(m_s.webcamBackground, key);
    bg->setPlaceholderText(QStringLiteral("None - the keyed area shows the screen"));
    auto* bgBrowse = new QPushButton(QStringLiteral("Browse..."), key);
    auto* bgClear = new QPushButton(QStringLiteral("Clear"), key);
    bgRow->addWidget(bg, 1);
    bgRow->addWidget(bgBrowse);
    bgRow->addWidget(bgClear);
    connect(bgBrowse, &QPushButton::clicked, this, [this, bg] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Background image"), QString(),
                                                          QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (!file.isEmpty()) {
            bg->setText(file);
            m_s.webcamBackground = file;
            webcamFiltersChanged();
        }
    });
    connect(bgClear, &QPushButton::clicked, this, [this, bg] {
        bg->clear();
        m_s.webcamBackground.clear();
        webcamFiltersChanged();
    });
    fk->addRow(QStringLiteral("Background image"), bgRow);
    fk->addRow(note(QStringLiteral("Needs an evenly lit green (or blue) backdrop. Background blur or removal without "
                                   "a green screen would need an AI segmentation model and is not included."),
                    key));
    tabs->addTab(key, QStringLiteral("Green screen"));
    tabs->setMinimumHeight(360);
    v->addWidget(tabs, 1);

    // ---- Toolbar actions --------------------------------------------------------
    connect(m_filtersOn, &QCheckBox::toggled, this, [this](bool on) {
        m_s.webcamFilters.enabled = on;
        webcamFiltersChanged();
    });
    connect(m_compareBox, &QCheckBox::toggled, this, [this](bool on) {
        m_webcam.setCompare(on);
        m_preview->setCompare(on);
    });
    connect(m_presetCombo, &QComboBox::activated, this, [this](int i) {
        if (i <= 0)
            return;
        applyWebcamFilterPreset(m_s.webcamFilters, m_presetCombo->itemText(i));
        m_s.webcamFilters.enabled = true;
        syncWebcamSliders();
        webcamFiltersChanged();
        m_autoInfo->setText(QStringLiteral("Preset \"%1\" applied. Fine-tune it below.").arg(m_presetCombo->itemText(i)));
        m_presetCombo->setCurrentIndex(0);
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        applyWebcamFilterPreset(m_s.webcamFilters, QStringLiteral("Natural"));
        syncWebcamSliders();
        webcamFiltersChanged();
        m_autoInfo->setText(QStringLiteral("Image adjustments reset."));
    });
    connect(autoBtn, &QPushButton::clicked, this, [this] {
        if (!m_webcam.running()) {
            m_autoInfo->setText(QStringLiteral("Start the camera first (it starts automatically while this page is open)."));
            return;
        }
        m_autoInfo->setText(QStringLiteral("Analysing the camera image..."));
        m_webcam.requestAutoAdjust();
    });
    connect(&m_webcam, &WebcamController::autoAdjustReady, this, [this](const webcam::FrameStats& st) {
        m_s.webcamFilters = webcam::suggestFilters(st, m_s.webcamFilters);
        syncWebcamSliders();
        webcamFiltersChanged();
        QString text = QStringLiteral("Auto adjust: scene brightness %1%, highlights %2%. Applied gamma %3, exposure %4 EV, "
                                      "contrast %5, colour balance R %6 / B %7.")
                           .arg(qRound(st.p50 * 100))
                           .arg(qRound(st.p98 * 100))
                           .arg(m_s.webcamFilters.gamma, 0, 'f', 2)
                           .arg(m_s.webcamFilters.exposure, 0, 'f', 2)
                           .arg(m_s.webcamFilters.contrast, 0, 'f', 2)
                           .arg(m_s.webcamFilters.redGain, 0, 'f', 2)
                           .arg(m_s.webcamFilters.blueGain, 0, 'f', 2);
        if (st.p50 < 0.2)
            text += QStringLiteral(" The scene is dark: expect visible noise; more light gives the biggest improvement.");
        m_autoInfo->setText(text);
    });
    return p;
}

void SettingsDialog::fillCameras()
{
    if (!m_camera)
        return;
    m_camera->clear();
    const auto& cams = m_scanner.cameras();
    if (cams.isEmpty()) {
        m_camera->addItem(QStringLiteral("No camera found"));
        m_camera->setEnabled(false);
        return;
    }
    m_camera->setEnabled(true);
    for (const CameraEntry& c : cams)
        m_camera->addItem(c.name, c.link);
    int idx = m_camera->findData(m_s.webcamLink);
    if (idx < 0) {
        idx = 0;
        m_s.webcamLink = cams.first().link;
        m_s.webcamName = cams.first().name;
    }
    m_camera->setCurrentIndex(idx);
    if (m_recording) {
        m_cameraMode->clear();
        m_cameraMode->addItem(QStringLiteral("%1 x %2 (in use)").arg(m_s.webcamMode.width).arg(m_s.webcamMode.height));
        return;
    }
    // Modes are read by opening the device; stop our own capture first so it is free.
    m_webcam.stopCamera();
    m_scanner.scanCameraModes(m_s.webcamLink);
}

void SettingsDialog::fillCameraModes(const QString& link, const QList<CameraModeEntry>& modes, const QString& error)
{
    if (!m_cameraMode || link != m_s.webcamLink)
        return;
    m_modes = modes;
    m_cameraMode->clear();
    if (!error.isEmpty() || modes.isEmpty()) {
        m_cameraMode->addItem(error.isEmpty() ? QStringLiteral("No modes reported") : QStringLiteral("Unavailable"));
        m_cameraState->setText(error);
        webcamFiltersChanged();
        return;
    }
    int select = -1;
    for (int i = 0; i < modes.size(); ++i) {
        const CameraModeEntry& e = modes[i];
        m_cameraMode->addItem(e.label());
        if (e.width == m_s.webcamMode.width && e.height == m_s.webcamMode.height &&
            e.fpsNum * m_s.webcamMode.fpsDen == m_s.webcamMode.fpsNum * e.fpsDen)
            select = i;
    }
    if (select < 0) {
        // Default: 1280x720 (or the largest <= 720p) at ~30 fps, else the first mode.
        for (int i = 0; i < modes.size() && select < 0; ++i)
            if (modes[i].height <= 720 && modes[i].fpsDen && modes[i].fpsNum / modes[i].fpsDen >= 25 &&
                modes[i].fpsNum / modes[i].fpsDen <= 30)
                select = i;
        if (select < 0)
            select = 0;
    }
    m_cameraMode->setCurrentIndex(select);
    const CameraModeEntry& e = modes[select];
    m_s.webcamMode = {e.width, e.height, e.fpsNum, e.fpsDen, e.format};
    webcamFiltersChanged();
}

void SettingsDialog::rebuildCameraControls()
{
    while (m_controlsForm->rowCount() > 0)
        m_controlsForm->removeRow(0);
    const auto controls = m_webcam.controls();
    if (controls.empty()) {
        m_controlsLoaded = false;
        m_controlsForm->addRow(note(m_webcam.running()
                                        ? QStringLiteral("This camera does not report adjustable hardware controls.")
                                        : QStringLiteral("The camera's own controls appear here once it is running."),
                                    m_controlsBox));
        return;
    }
    m_controlsLoaded = true;
    using Kind = webcam::CameraControl::Kind;
    // Lighting-related controls first, with plain-language choices.
    for (const webcam::CameraControl& c : controls) {
        if (c.kind == Kind::PowerLine) {
            auto* combo = new QComboBox(m_controlsBox);
            combo->addItems({QStringLiteral("Off"), QStringLiteral("50 Hz (Europe, Asia, Africa, Australia)"),
                             QStringLiteral("60 Hz (Americas, parts of Japan)")});
            combo->setCurrentIndex(std::clamp(static_cast<int>(c.value), 0, 2));
            combo->setToolTip(QStringLiteral("Match your mains frequency so the camera's auto exposure avoids "
                                             "flicker and rolling bands under electric lights"));
            connect(combo, &QComboBox::activated, this, [this, c](int i) {
                webcam::CameraControl n = c;
                n.value = i;
                m_webcam.setControl(n);
            });
            m_controlsForm->addRow(QStringLiteral("Anti-flicker"), combo);
        } else if (c.kind == Kind::ExposurePriority) {
            auto* combo = new QComboBox(m_controlsBox);
            combo->addItems({QStringLiteral("Smooth motion - keep the full frame rate (darker in dim light)"),
                             QStringLiteral("Brighter image - camera may expose longer (blur / stutter in dim light)")});
            combo->setCurrentIndex(c.value ? 1 : 0);
            combo->setToolTip(QStringLiteral("In dim rooms many webcams lengthen the exposure beyond one frame. "
                                             "That brightens the picture but smears motion and repeats frames."));
            connect(combo, &QComboBox::activated, this, [this, c](int i) {
                webcam::CameraControl n = c;
                n.value = i;
                m_webcam.setControl(n);
            });
            m_controlsForm->addRow(QStringLiteral("Low light"), combo);
        }
    }
    for (const webcam::CameraControl& c : controls) {
        if (c.kind == Kind::PowerLine || c.kind == Kind::ExposurePriority)
            continue;
        auto* row = new QWidget(m_controlsBox);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* s = new QSlider(Qt::Horizontal, row);
        s->setRange(static_cast<int>(c.min), static_cast<int>(c.max));
        s->setSingleStep(static_cast<int>(std::max(1L, c.step)));
        s->setPageStep(static_cast<int>(std::max(1L, (c.max - c.min) / 10)));
        s->setValue(static_cast<int>(c.value));
        s->setAccessibleName(QString::fromStdString(c.name));
        auto* value = new QLabel(QString::number(c.value), row);
        value->setMinimumWidth(40);
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
        QString label = QString::fromStdString(c.name);
        if (c.kind == Kind::Camera && c.name == "Exposure")
            label = QStringLiteral("Exposure (2^n s)");
        m_controlsForm->addRow(label, row);
    }
    m_controlsForm->addRow(note(QStringLiteral("These are the camera's own settings (only those it reports). They "
                                               "take effect immediately and are remembered by the camera driver."),
                                m_controlsBox));
}

// ----------------------------------------------------------------- Hotkeys

QWidget* SettingsDialog::buildHotkeys()
{
    QVBoxLayout* v = nullptr;
    QWidget* p = page(QStringLiteral("Hotkeys"), v);
    auto* box = new QGroupBox(QStringLiteral("Global hotkeys (work while other programs are focused)"), p);
    auto* form = new QFormLayout(box);
    for (int i = 0; i < static_cast<int>(HotkeyAction::Count); ++i) {
        auto* row = new QWidget(box);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* edit = new QKeySequenceEdit(m_s.hotkeys[i], row);
        edit->setMaximumSequenceLength(1);
        edit->setAccessibleName(AppSettings::hotkeyName(static_cast<HotkeyAction>(i)));
        auto* clear = new QPushButton(QStringLiteral("Clear"), row);
        h->addWidget(edit, 1);
        h->addWidget(clear);
        connect(clear, &QPushButton::clicked, edit, &QKeySequenceEdit::clear);
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this, &SettingsDialog::updateHotkeyConflicts);
        m_hotkeyEdits[i] = edit;
        form->addRow(AppSettings::hotkeyName(static_cast<HotkeyAction>(i)), row);
    }
    v->addWidget(box);
    m_hotkeyConflicts = new QLabel(p);
    m_hotkeyConflicts->setObjectName("Warn");
    m_hotkeyConflicts->setWordWrap(true);
    v->addWidget(m_hotkeyConflicts);
    v->addWidget(note(QStringLiteral("Use a modifier (Ctrl / Alt / Shift) to avoid clashing with normal typing. "
                                     "If another program already owns a combination, Windows refuses it and "
                                     "LumaCapture shows a warning after you apply."),
                      p));
    v->addStretch();
    updateHotkeyConflicts();
    return p;
}

void SettingsDialog::updateHotkeyConflicts()
{
    if (!m_hotkeyConflicts)
        return;
    QStringList problems;
    for (int i = 0; i < static_cast<int>(HotkeyAction::Count); ++i) {
        if (!m_hotkeyEdits[i])
            continue;
        const QKeySequence a = m_hotkeyEdits[i]->keySequence();
        if (a.isEmpty())
            continue;
        unsigned mods = 0, vk = 0;
        if (!HotkeyManager::toNative(a, mods, vk))
            problems << QStringLiteral("%1 cannot be used as a global hotkey.").arg(a.toString(QKeySequence::NativeText));
        for (int j = i + 1; j < static_cast<int>(HotkeyAction::Count); ++j)
            if (m_hotkeyEdits[j] && m_hotkeyEdits[j]->keySequence() == a)
                problems << QStringLiteral("%1 is assigned to both \"%2\" and \"%3\".")
                                .arg(a.toString(QKeySequence::NativeText),
                                     AppSettings::hotkeyName(static_cast<HotkeyAction>(i)),
                                     AppSettings::hotkeyName(static_cast<HotkeyAction>(j)));
    }
    m_hotkeyConflicts->setText(problems.join('\n'));
}

// ---------------------------------------------------------------- Advanced

QWidget* SettingsDialog::buildAdvanced()
{
    QVBoxLayout* v = nullptr;
    QWidget* p = page(QStringLiteral("Advanced"), v);
    auto* box = new QGroupBox(QStringLiteral("Pipeline"), p);
    auto* form = new QFormLayout(box);
    auto* cpu = new QCheckBox(QStringLiteral("Convert colours on the CPU instead of the GPU"), box);
    cpu->setChecked(m_s.cpuConvert);
    cpu->setToolTip(QStringLiteral("Fallback only. Measured on this PC: about 21 ms of CPU per 1080p frame versus "
                                   "about 1 ms with GPU conversion."));
    connect(cpu, &QCheckBox::toggled, this, [this](bool on) { m_s.cpuConvert = on; });
    form->addRow(cpu);
    auto* queue = new QSpinBox(box);
    queue->setRange(2, 32);
    queue->setValue(m_s.queueCapacity);
    queue->setSuffix(QStringLiteral(" frames"));
    queue->setToolTip(QStringLiteral("Bounded buffer between capture and encoder. When full, the newest frame is "
                                     "dropped instead of letting memory and latency grow."));
    connect(queue, &QSpinBox::valueChanged, this, [this](int n) { m_s.queueCapacity = n; });
    form->addRow(QStringLiteral("Encoder queue"), queue);
    v->addWidget(box);

    auto* files = new QGroupBox(QStringLiteral("Files && diagnostics"), p);
    auto* f2 = new QVBoxLayout(files);
    auto* openData = new QPushButton(QStringLiteral("Open settings / log folder"), files);
    connect(openData, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::dataDir())); });
    f2->addWidget(openData);
    auto* recovery = new QPushButton(QStringLiteral("Look for interrupted recordings to recover"), files);
    connect(recovery, &QPushButton::clicked, this, &SettingsDialog::recoveryScanRequested);
    f2->addWidget(recovery);
    f2->addWidget(note(QStringLiteral("Settings, history and logs are stored in LumaCapture-data next to "
                                      "LumaCapture.exe. Nothing is sent over the network."),
                       files));
    v->addWidget(files);
    v->addStretch();
    return p;
}

// ------------------------------------------------------------------- About

QWidget* SettingsDialog::buildAbout()
{
    QVBoxLayout* v = nullptr;
    QWidget* p = page(QStringLiteral("About & licenses"), v);
    auto* text = new QTextBrowser(p);
    text->setOpenExternalLinks(false);
    text->setHtml(QStringLiteral(
        "<h3>LumaCapture %1</h3>"
        "<p>A lightweight screen recorder built for older Windows PCs.</p>"
        "<h4>Licensing</h4>"
        "<p>This build of LumaCapture uses FFmpeg 7.1 configured with <b>--enable-gpl --enable-version3</b> and "
        "<b>libx264</b>. Distributing LumaCapture together with these libraries is therefore subject to the "
        "<b>GNU General Public License, version 3</b>.</p>"
        "<ul>"
        "<li><b>FFmpeg</b> (libavcodec, libavformat, libavutil, libavfilter, libswscale, libswresample, "
        "libpostproc) - GPL v3 build. https://ffmpeg.org</li>"
        "<li><b>x264</b> (inside libavcodec) - GNU GPL v2 or later. https://www.videolan.org/developers/x264.html</li>"
        "<li><b>Qt 6.8</b> (Core, Gui, Widgets, Concurrent), dynamically linked - GNU LGPL v3. https://www.qt.io</li>"
        "<li><b>Microsoft Visual C++ runtime</b> - redistributed under the Visual Studio license terms.</li>"
        "<li>Windows APIs used: Desktop Duplication, Windows.Graphics.Capture, Direct3D 11, WASAPI, "
        "Media Foundation (part of Windows).</li>"
        "</ul>"
        "<p>The full license texts are in the <b>licenses</b> folder next to LumaCapture.exe.</p>"
        "<p>No hardware video encoder is used: the GeForce GT 730 (Fermi) has no NVENC. Video is encoded in "
        "software with x264.</p>")
                      .arg(QStringLiteral(LUMACAPTURE_VERSION)));
    v->addWidget(text, 1);
    auto* open = new QPushButton(QStringLiteral("Open license files"), p);
    connect(open, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::licensesDir())); });
    v->addWidget(open);
    return p;
}

} // namespace luma::app
