#include "ui/ContextPanels.h"

#include "AppSettings.h"
#include "DeviceScanner.h"
#include "LayoutDialog.h"
#include "RecordingController.h"
#include "SettingsStore.h"
#include "Theme.h"
#include "WebcamController.h"
#include "widgets/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace luma::app {
namespace {

gpu::Rgba toRgba(const QColor& c)
{
    return {static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF()),
            static_cast<float>(c.alphaF())};
}

QColor toQColor(const gpu::Rgba& c)
{
    return QColor::fromRgbF(c.r, c.g, c.b, c.a);
}

QString percent(int v)
{
    return QStringLiteral("%1%").arg(v);
}

QString signedValue(int v)
{
    return v > 0 ? QStringLiteral("+%1").arg(v) : QString::number(v);
}

QString hundredths(int v)
{
    return QString::number(v / 100.0, 'f', 2);
}

QString decibels(int v)
{
    return QStringLiteral("%1 dB").arg(v);
}

void setChecked(QCheckBox* box, bool on)
{
    const QSignalBlocker block(box);
    box->setChecked(on);
}

void setSpin(QSpinBox* spin, int v)
{
    const QSignalBlocker block(spin);
    spin->setValue(v);
}

QVBoxLayout* pageLayout(QWidget* w)
{
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 4, 0);
    v->setSpacing(10);
    return v;
}

QColor levelColor(PerfLevel level)
{
    const Palette& pal = currentPalette();
    switch (level) {
    case PerfLevel::Light:
    case PerfLevel::Moderate: return pal.success;
    case PerfLevel::Heavy: return pal.warning;
    case PerfLevel::VeryHeavy: return pal.record;
    }
    return pal.textDim;
}

} // namespace

// --------------------------------------------------------------- combo fillers

void fillResolutionCombo(QComboBox* c)
{
    for (int i = 0; i <= static_cast<int>(ResolutionPreset::P480); ++i)
        c->addItem(resolutionName(static_cast<ResolutionPreset>(i)), i);
    c->setToolTip(QStringLiteral("Output size. The source's aspect ratio is kept and nothing is upscaled."));
}

void fillFpsCombo(QComboBox* c)
{
    for (int fps : kFrameRates)
        c->addItem(fps >= 50 ? QStringLiteral("%1 FPS (needs more CPU)").arg(fps) : QStringLiteral("%1 FPS").arg(fps), fps);
    c->setToolTip(QStringLiteral("Frames per second. 30 FPS is recommended; 50/60 FPS work best at 1280×720 on a "
                                 "quad-core CPU like this one."));
}

void fillQualityCombo(QComboBox* c)
{
    for (int i = 0; i <= static_cast<int>(QualityPreset::Custom); ++i)
        c->addItem(qualityName(static_cast<QualityPreset>(i)), i);
    c->setToolTip(QStringLiteral("Low = smallest files; Balanced = recommended; High / Very High = sharper, larger "
                                 "files. Custom uses the CRF value under Advanced."));
}

void fillPresetCombo(QComboBox* c)
{
    c->addItem(QStringLiteral("Auto (recommended)"), QStringLiteral("auto"));
    c->addItem(QStringLiteral("Ultrafast"), QStringLiteral("ultrafast"));
    c->addItem(QStringLiteral("Superfast (smaller files, more CPU)"), QStringLiteral("superfast"));
    c->addItem(QStringLiteral("Veryfast (smallest files, much more CPU)"), QStringLiteral("veryfast"));
    c->setToolTip(QStringLiteral("x264 speed preset. Auto uses \"ultrafast\" with the deblocking filter on, which "
                                 "gave the best quality per CPU second in tests on this PC."));
}

// ------------------------------------------------------------------ VideoPanel

VideoPanel::VideoPanel(SettingsStore& store, RecordingController& recording, QWidget* parent)
    : QWidget(parent), m_store(store), m_rec(recording)
{
    QVBoxLayout* v = pageLayout(this);

    auto* main = new CollapsibleSection(QStringLiteral("Video settings"), this);
    m_resolution = new QComboBox;
    fillResolutionCombo(m_resolution);
    m_fps = new QComboBox;
    fillFpsCombo(m_fps);
    m_quality = new QComboBox;
    fillQualityCombo(m_quality);
    auto* encoder = new QLabel(QStringLiteral("H.264 (x264, CPU)"));
    encoder->setToolTip(QStringLiteral("Video is encoded on the CPU with x264. The GeForce GT 730 (Fermi) in this PC "
                                       "has no hardware video encoder, so none is offered."));
    m_preset = new QComboBox;
    fillPresetCombo(m_preset);
    m_container = new QComboBox;
    m_container->addItem(QStringLiteral("MKV (recommended)"), 0);
    m_container->addItem(QStringLiteral("MP4 (converted after recording)"), 1);
    m_container->setToolTip(QStringLiteral("MKV survives crashes and power loss. MP4 is created from the MKV when "
                                           "the recording stops."));
    main->body()->addWidget(ui::row(QStringLiteral("Resolution"), m_resolution, main));
    main->body()->addWidget(ui::row(QStringLiteral("Frame rate"), m_fps, main));
    main->body()->addWidget(ui::row(QStringLiteral("Quality"), m_quality, main));
    main->body()->addWidget(ui::row(QStringLiteral("Encoder"), encoder, main));
    main->body()->addWidget(ui::row(QStringLiteral("Preset"), m_preset, main));
    main->body()->addWidget(ui::row(QStringLiteral("Container"), m_container, main));
    m_perf = new QLabel(main);
    m_perf->setTextFormat(Qt::RichText);
    m_perf->setWordWrap(true);
    m_advice = new QLabel(main);
    m_advice->setObjectName("Warn");
    m_advice->setWordWrap(true);
    main->body()->addWidget(m_perf);
    main->body()->addWidget(m_advice);
    v->addWidget(main);

    m_advanced = new CollapsibleSection(QStringLiteral("Advanced encoder options"), this, store.get().advancedVideo);
    m_crf = new QSpinBox;
    m_crf->setRange(0, 51);
    m_keyframe = new QDoubleSpinBox;
    m_keyframe->setRange(0.5, 10.0);
    m_keyframe->setSingleStep(0.5);
    m_keyframe->setSuffix(QStringLiteral(" s"));
    m_threads = new QSpinBox;
    m_threads->setRange(1, 8);
    m_queue = new QSpinBox;
    m_queue->setRange(2, 64);
    m_queue->setSuffix(QStringLiteral(" frames"));
    m_gpuConvert = new QCheckBox(QStringLiteral("GPU colour conversion (recommended)"));
    m_advanced->body()->addWidget(ui::row(QStringLiteral("CRF"), m_crf, m_advanced,
                                          QStringLiteral("Constant rate factor: lower = better quality and larger "
                                                         "files. Changing it switches Quality to Custom.")));
    m_advanced->body()->addWidget(ui::row(QStringLiteral("Keyframe every"), m_keyframe, m_advanced,
                                          QStringLiteral("Shorter = easier seeking, slightly larger files.")));
    m_advanced->body()->addWidget(ui::row(QStringLiteral("Encoder threads"), m_threads, m_advanced,
                                          QStringLiteral("3 leaves one core for capture, audio and the UI on a "
                                                         "4-core CPU.")));
    m_advanced->body()->addWidget(ui::row(QStringLiteral("Frame queue"), m_queue, m_advanced,
                                          QStringLiteral("Frames that may wait for the encoder before new ones are "
                                                         "dropped (bounds the delay; never grows without limit).")));
    m_gpuConvert->setParent(m_advanced);
    m_gpuConvert->setToolTip(QStringLiteral("Converts the picture to the encoder's format on the graphics card "
                                            "(less CPU). Off = CPU conversion (fallback; also disables the live preview)."));
    m_advanced->body()->addWidget(m_gpuConvert);
    v->addWidget(m_advanced);

    auto* guide = new CollapsibleSection(QStringLiteral("Resolution guide (this PC)"), this);
    m_guide = new QLabel(guide);
    m_guide->setTextFormat(Qt::RichText);
    m_guide->setWordWrap(true);
    guide->body()->addWidget(m_guide);
    guide->body()->addWidget(ui::hint(QStringLiteral("Estimated from measurements on an Intel i5-2400S (software "
                                                     "encoding). Fast motion and a hot CPU need more headroom."),
                                      guide));
    v->addWidget(guide);
    v->addStretch();

    auto editVideo = [this](auto change) {
        if (m_updating)
            return;
        m_store.edit(SettingsStore::Video, [&](AppSettings& s) {
            change(s);
            s.profile.clear(); // individual changes no longer match a named profile
        });
    };
    connect(m_resolution, &QComboBox::activated, this, [this, editVideo](int i) {
        const int r = m_resolution->itemData(i).toInt();
        editVideo([r](AppSettings& s) { s.resolution = static_cast<ResolutionPreset>(r); });
    });
    connect(m_fps, &QComboBox::activated, this, [this, editVideo](int i) {
        const int fps = m_fps->itemData(i).toInt();
        editVideo([fps](AppSettings& s) { s.fps = fps; });
    });
    connect(m_quality, &QComboBox::activated, this, [this, editVideo](int i) {
        const int q = m_quality->itemData(i).toInt();
        editVideo([q](AppSettings& s) { s.quality = static_cast<QualityPreset>(q); });
    });
    connect(m_preset, &QComboBox::activated, this, [this, editVideo](int i) {
        const QString p = m_preset->itemData(i).toString();
        editVideo([p](AppSettings& s) { s.x264Preset = p; });
    });
    connect(m_container, &QComboBox::activated, this, [this](int i) {
        if (m_updating)
            return;
        const int c = m_container->itemData(i).toInt();
        m_store.edit(SettingsStore::Output, [c](AppSettings& s) { s.container = static_cast<Container>(c); });
    });
    connect(m_crf, &QSpinBox::valueChanged, this, [editVideo](int v) {
        editVideo([v](AppSettings& s) {
            s.crf = v;
            s.quality = QualityPreset::Custom;
        });
    });
    connect(m_keyframe, &QDoubleSpinBox::valueChanged, this, [editVideo](double v) {
        editVideo([v](AppSettings& s) { s.keyframeSeconds = v; });
    });
    connect(m_threads, &QSpinBox::valueChanged, this, [editVideo](int v) {
        editVideo([v](AppSettings& s) { s.encoderThreads = v; });
    });
    connect(m_queue, &QSpinBox::valueChanged, this, [editVideo](int v) {
        editVideo([v](AppSettings& s) { s.queueCapacity = v; });
    });
    connect(m_gpuConvert, &QCheckBox::toggled, this, [editVideo](bool on) {
        editVideo([on](AppSettings& s) { s.cpuConvert = !on; });
    });
    connect(m_advanced, &CollapsibleSection::toggled, this, [this](bool on) {
        m_store.edit(SettingsStore::Ui, [on](AppSettings& s) { s.advancedVideo = on; });
    });
    connect(&m_store, &SettingsStore::changed, this, [this](unsigned scope) {
        if (scope & (SettingsStore::Video | SettingsStore::Source | SettingsStore::Output))
            refresh();
    });
    refresh();
}

void VideoPanel::refresh()
{
    ui::Updating guard(m_updating);
    const AppSettings& s = m_store.get();
    ui::selectData(m_resolution, static_cast<int>(s.resolution));
    ui::selectData(m_fps, s.fps);
    ui::selectData(m_quality, static_cast<int>(s.quality));
    if (!ui::selectData(m_preset, s.x264Preset))
        ui::selectData(m_preset, QStringLiteral("auto"));
    ui::selectData(m_container, static_cast<int>(s.container));
    setSpin(m_crf, resolveEncoder(s).crf);
    {
        const QSignalBlocker block(m_keyframe);
        m_keyframe->setValue(s.keyframeSeconds);
    }
    setSpin(m_threads, s.encoderThreads);
    setSpin(m_queue, s.queueCapacity);
    setChecked(m_gpuConvert, !s.cpuConvert);
    updatePerformance();
}

void VideoPanel::setBusy(bool busy)
{
    // The encoder is configured when the recording starts.
    for (QWidget* w : std::initializer_list<QWidget*>{m_resolution, m_fps, m_quality, m_preset, m_container, m_crf,
                                                      m_keyframe, m_threads, m_queue, m_gpuConvert})
        w->setEnabled(!busy);
}

void VideoPanel::updatePerformance()
{
    const AppSettings& s = m_store.get();
    const QSize out = m_rec.outputSize();
    const EncoderChoice enc = resolveEncoder(s);
    const PerfEstimate pe = estimatePerformance(out, s.fps, enc);
    const QString size = out.isValid() ? QStringLiteral("%1×%2").arg(out.width()).arg(out.height()) : QStringLiteral("-");
    m_perf->setText(QStringLiteral("<span style='color:%1'>●</span> %2 · %3 FPS · %4 &nbsp;—&nbsp; Performance: <b>%5</b>")
                        .arg(levelColor(pe.level).name(), size)
                        .arg(s.fps)
                        .arg(qualityName(s.quality), pe.label()));
    m_perf->setToolTip(enc.description());
    m_advice->setText(pe.advice());
    m_advice->setVisible(!pe.advice().isEmpty());

    // Resolution guide for the current source.
    const QSize src = m_rec.sourceSize();
    if (!src.isValid() || src.isEmpty()) {
        m_guide->setText(QStringLiteral("Choose a source to see estimates."));
        return;
    }
    struct Entry {
        ResolutionPreset res;
        int fps;
    };
    const Entry entries[] = {{ResolutionPreset::P720, 30},
                             {ResolutionPreset::P1080, 30},
                             {ResolutionPreset::P720, 60},
                             {ResolutionPreset::P1080, 60}};
    QString html = QStringLiteral("<table cellspacing='0' cellpadding='2'>");
    QSize lastOut;
    int lastFps = 0;
    for (const Entry& e : entries) {
        const QSize o = scaledOutputSize(src, e.res);
        if (o == lastOut && e.fps == lastFps)
            continue;
        lastOut = o;
        lastFps = e.fps;
        const PerfEstimate g = estimatePerformance(o, e.fps, enc);
        const QString verdict = g.level == PerfLevel::Light || g.level == PerfLevel::Moderate
                                    ? QStringLiteral("Recommended")
                                    : (g.level == PerfLevel::Heavy ? QStringLiteral("Heavy - may drop frames")
                                                                   : QStringLiteral("Not recommended (experimental)"));
        html += QStringLiteral("<tr><td><span style='color:%1'>●</span> %2p%3</td><td>&nbsp;&nbsp;%4</td></tr>")
                    .arg(levelColor(g.level).name())
                    .arg(o.height())
                    .arg(e.fps)
                    .arg(verdict);
    }
    html += QStringLiteral("</table>");
    m_guide->setText(html);
}

// ------------------------------------------------------------------ AudioPanel

AudioPanel::AudioPanel(SettingsStore& store, DeviceScanner& scanner, QWidget* parent)
    : QWidget(parent), m_store(store), m_scanner(scanner)
{
    QVBoxLayout* v = pageLayout(this);

    auto* sys = new CollapsibleSection(QStringLiteral("System audio"), this);
    m_system = new ToggleSwitch;
    m_systemDevice = new QComboBox;
    m_systemVol = ui::sliderRow(QStringLiteral("Volume"), 0, 200, sys, percent,
                                QStringLiteral("Level of the system audio in the recording (100% = unchanged)"));
    sys->body()->addWidget(ui::row(QStringLiteral("Record"), m_system, sys,
                                   QStringLiteral("Record what you hear (WASAPI loopback)")));
    sys->body()->addWidget(ui::row(QStringLiteral("Device"), m_systemDevice, sys));
    sys->body()->addWidget(m_systemVol.widget);
    v->addWidget(sys);

    auto* mic = new CollapsibleSection(QStringLiteral("Microphone"), this);
    m_mic = new ToggleSwitch;
    m_micDevice = new QComboBox;
    m_micVol = ui::sliderRow(QStringLiteral("Volume"), 0, 200, mic, percent);
    m_micMuted = new QCheckBox(QStringLiteral("Muted (Ctrl+Alt+M)"));
    m_micDelay = new QSpinBox;
    m_micDelay->setRange(-500, 500);
    m_micDelay->setSuffix(QStringLiteral(" ms"));
    mic->body()->addWidget(ui::row(QStringLiteral("Record"), m_mic, mic));
    mic->body()->addWidget(ui::row(QStringLiteral("Device"), m_micDevice, mic));
    mic->body()->addWidget(m_micVol.widget);
    m_micMuted->setParent(mic);
    mic->body()->addWidget(m_micMuted);
    mic->body()->addWidget(ui::row(QStringLiteral("Sync delay"), m_micDelay, mic,
                                   QStringLiteral("Shifts the microphone if your voice is early/late against the "
                                                  "picture (USB mics often need +50..+150 ms).")));
    v->addWidget(mic);

    m_test = new QPushButton(QStringLiteral("Test levels"), this);
    m_test->setCheckable(true);
    m_test->setToolTip(QStringLiteral("Shows the levels without recording (opens the devices; nothing is saved)"));
    v->addWidget(m_test);

    auto* proc = new CollapsibleSection(QStringLiteral("Voice processing (uses CPU)"), this, false);
    m_proc = new QCheckBox(QStringLiteral("Enable microphone processing"), proc);
    m_noise = new QCheckBox(QStringLiteral("Noise suppression (FFmpeg afftdn)"), proc);
    m_noiseDb = ui::sliderRow(QStringLiteral("Reduction"), 3, 40, proc, decibels);
    m_highPass = new QCheckBox(QStringLiteral("Rumble filter (high-pass 80 Hz)"), proc);
    m_gate = new QCheckBox(QStringLiteral("Noise gate"), proc);
    m_gateThr = ui::sliderRow(QStringLiteral("Gate threshold"), -80, -10, proc, decibels);
    m_comp = new QCheckBox(QStringLiteral("Compressor (evens out loudness)"), proc);
    m_compThr = ui::sliderRow(QStringLiteral("Threshold"), -50, 0, proc, decibels);
    m_compRatio = ui::sliderRow(QStringLiteral("Ratio"), 10, 100, proc,
                                [](int v) { return QStringLiteral("%1:1").arg(v / 10.0, 0, 'f', 1); });
    m_limiter = new QCheckBox(QStringLiteral("Limiter (prevents clipping)"), proc);
    m_gain = ui::sliderRow(QStringLiteral("Input gain"), -20, 30, proc, decibels);
    m_eq = new QCheckBox(QStringLiteral("Equalizer"), proc);
    m_eqLow = ui::sliderRow(QStringLiteral("Low"), -12, 12, proc, decibels);
    m_eqMid = ui::sliderRow(QStringLiteral("Mid"), -12, 12, proc, decibels);
    m_eqHigh = ui::sliderRow(QStringLiteral("High"), -12, 12, proc, decibels);
    for (QWidget* w : std::initializer_list<QWidget*>{m_proc, m_noise, m_noiseDb.widget, m_highPass, m_gate,
                                                      m_gateThr.widget, m_comp, m_compThr.widget, m_compRatio.widget,
                                                      m_limiter, m_gain.widget, m_eq, m_eqLow.widget, m_eqMid.widget,
                                                      m_eqHigh.widget})
        proc->body()->addWidget(w);
    v->addWidget(proc);

    auto* enc = new CollapsibleSection(QStringLiteral("Audio encoding"), this, false);
    m_bitrate = new QComboBox;
    for (int kbps : {96, 128, 160, 192, 256})
        m_bitrate->addItem(QStringLiteral("%1 kbit/s").arg(kbps), kbps);
    m_separate = new QCheckBox(QStringLiteral("Also store system audio and microphone as separate tracks"), enc);
    enc->body()->addWidget(ui::row(QStringLiteral("AAC bitrate"), m_bitrate, enc));
    enc->body()->addWidget(m_separate);
    v->addWidget(enc);
    v->addStretch();

    auto edit = [this](auto change) {
        if (m_updating)
            return;
        m_store.edit(SettingsStore::Audio, change);
    };
    connect(m_system, &ToggleSwitch::toggled, this, [edit](bool on) { edit([on](AppSettings& s) { s.systemAudio = on; }); });
    connect(m_mic, &ToggleSwitch::toggled, this, [edit](bool on) { edit([on](AppSettings& s) { s.microphone = on; }); });
    connect(m_systemDevice, &QComboBox::activated, this, [this, edit](int i) {
        const QString id = m_systemDevice->itemData(i).toString();
        edit([id](AppSettings& s) { s.systemDevice = id; });
    });
    connect(m_micDevice, &QComboBox::activated, this, [this, edit](int i) {
        const QString id = m_micDevice->itemData(i).toString();
        edit([id](AppSettings& s) { s.micDevice = id; });
    });
    connect(m_systemVol.slider, &QSlider::valueChanged, this,
            [edit](int v) { edit([v](AppSettings& s) { s.systemVolume = v / 100.0; }); });
    connect(m_micVol.slider, &QSlider::valueChanged, this,
            [edit](int v) { edit([v](AppSettings& s) { s.micVolume = v / 100.0; }); });
    connect(m_micMuted, &QCheckBox::toggled, this, [edit](bool on) { edit([on](AppSettings& s) { s.micMuted = on; }); });
    connect(m_micDelay, &QSpinBox::valueChanged, this, [edit](int v) { edit([v](AppSettings& s) { s.micDelayMs = v; }); });
    connect(m_test, &QPushButton::toggled, this, &AudioPanel::testLevelsToggled);
    connect(m_proc, &QCheckBox::toggled, this, [edit](bool on) { edit([on](AppSettings& s) { s.micFilters.enabled = on; }); });
    connect(m_noise, &QCheckBox::toggled, this,
            [edit](bool on) { edit([on](AppSettings& s) { s.micFilters.noiseSuppression = on; }); });
    connect(m_noiseDb.slider, &QSlider::valueChanged, this,
            [edit](int v) { edit([v](AppSettings& s) { s.micFilters.noiseReductionDb = static_cast<float>(v); }); });
    connect(m_highPass, &QCheckBox::toggled, this, [edit](bool on) { edit([on](AppSettings& s) { s.micFilters.highPass = on; }); });
    connect(m_gate, &QCheckBox::toggled, this, [edit](bool on) { edit([on](AppSettings& s) { s.micFilters.gate = on; }); });
    connect(m_gateThr.slider, &QSlider::valueChanged, this,
            [edit](int v) { edit([v](AppSettings& s) { s.micFilters.gateThresholdDb = static_cast<float>(v); }); });
    connect(m_comp, &QCheckBox::toggled, this, [edit](bool on) { edit([on](AppSettings& s) { s.micFilters.compressor = on; }); });
    connect(m_compThr.slider, &QSlider::valueChanged, this,
            [edit](int v) { edit([v](AppSettings& s) { s.micFilters.compThresholdDb = static_cast<float>(v); }); });
    connect(m_compRatio.slider, &QSlider::valueChanged, this,
            [edit](int v) { edit([v](AppSettings& s) { s.micFilters.compRatio = v / 10.f; }); });
    connect(m_limiter, &QCheckBox::toggled, this, [edit](bool on) { edit([on](AppSettings& s) { s.micFilters.limiter = on; }); });
    connect(m_gain.slider, &QSlider::valueChanged, this,
            [edit](int v) { edit([v](AppSettings& s) { s.micFilters.gainDb = static_cast<float>(v); }); });
    connect(m_eq, &QCheckBox::toggled, this, [edit](bool on) { edit([on](AppSettings& s) { s.micFilters.eq = on; }); });
    connect(m_eqLow.slider, &QSlider::valueChanged, this,
            [edit](int v) { edit([v](AppSettings& s) { s.micFilters.eqLowDb = static_cast<float>(v); }); });
    connect(m_eqMid.slider, &QSlider::valueChanged, this,
            [edit](int v) { edit([v](AppSettings& s) { s.micFilters.eqMidDb = static_cast<float>(v); }); });
    connect(m_eqHigh.slider, &QSlider::valueChanged, this,
            [edit](int v) { edit([v](AppSettings& s) { s.micFilters.eqHighDb = static_cast<float>(v); }); });
    connect(m_bitrate, &QComboBox::activated, this, [this, edit](int i) {
        const int kbps = m_bitrate->itemData(i).toInt();
        edit([kbps](AppSettings& s) { s.audioBitrate = kbps; });
    });
    connect(m_separate, &QCheckBox::toggled, this, [edit](bool on) { edit([on](AppSettings& s) { s.separateTracks = on; }); });

    connect(&m_store, &SettingsStore::changed, this, [this](unsigned scope) {
        if (scope & SettingsStore::Audio)
            refresh();
    });
    connect(&m_scanner, &DeviceScanner::audioReady, this, &AudioPanel::refreshDevices);
    refreshDevices();
    refresh();
}

void AudioPanel::refreshDevices()
{
    auto fill = [](QComboBox* combo, const QList<AudioEntry>& list) {
        const QSignalBlocker block(combo);
        combo->clear();
        combo->addItem(QStringLiteral("Windows default device"), QString());
        for (const AudioEntry& e : list)
            combo->addItem(e.name, e.id);
    };
    fill(m_systemDevice, m_scanner.renderDevices());
    fill(m_micDevice, m_scanner.captureDevices());
    refresh();
}

void AudioPanel::refresh()
{
    ui::Updating guard(m_updating);
    const AppSettings& s = m_store.get();
    {
        const QSignalBlocker b1(m_system), b2(m_mic);
        m_system->setChecked(s.systemAudio);
        m_mic->setChecked(s.microphone);
    }
    if (!ui::selectData(m_systemDevice, s.systemDevice))
        ui::selectData(m_systemDevice, QString());
    if (!ui::selectData(m_micDevice, s.micDevice))
        ui::selectData(m_micDevice, QString());
    m_systemVol.setValue(static_cast<int>(std::lround(s.systemVolume * 100)));
    m_micVol.setValue(static_cast<int>(std::lround(s.micVolume * 100)));
    setChecked(m_micMuted, s.micMuted);
    setSpin(m_micDelay, s.micDelayMs);
    const auto& m = s.micFilters;
    setChecked(m_proc, m.enabled);
    setChecked(m_noise, m.noiseSuppression);
    m_noiseDb.setValue(static_cast<int>(m.noiseReductionDb));
    setChecked(m_highPass, m.highPass);
    setChecked(m_gate, m.gate);
    m_gateThr.setValue(static_cast<int>(m.gateThresholdDb));
    setChecked(m_comp, m.compressor);
    m_compThr.setValue(static_cast<int>(m.compThresholdDb));
    m_compRatio.setValue(static_cast<int>(std::lround(m.compRatio * 10)));
    setChecked(m_limiter, m.limiter);
    m_gain.setValue(static_cast<int>(m.gainDb));
    setChecked(m_eq, m.eq);
    m_eqLow.setValue(static_cast<int>(m.eqLowDb));
    m_eqMid.setValue(static_cast<int>(m.eqMidDb));
    m_eqHigh.setValue(static_cast<int>(m.eqHighDb));
    ui::selectData(m_bitrate, s.audioBitrate);
    setChecked(m_separate, s.separateTracks);
    // Processing controls only matter when processing is on.
    for (QWidget* w : std::initializer_list<QWidget*>{m_noise, m_noiseDb.widget, m_highPass, m_gate, m_gateThr.widget,
                                                      m_comp, m_compThr.widget, m_compRatio.widget, m_limiter,
                                                      m_gain.widget, m_eq, m_eqLow.widget, m_eqMid.widget,
                                                      m_eqHigh.widget})
        w->setEnabled(m.enabled);
    m_micDevice->setEnabled(s.microphone && m_test->isEnabled());
    m_systemDevice->setEnabled(s.systemAudio && m_test->isEnabled());
}

void AudioPanel::setBusy(bool busy)
{
    // Devices and tracks are opened when the recording starts; volumes, mute and
    // processing apply live.
    for (QWidget* w : std::initializer_list<QWidget*>{m_system, m_mic, m_systemDevice, m_micDevice, m_test, m_bitrate,
                                                      m_separate, m_micDelay})
        w->setEnabled(!busy);
    if (busy)
        setTestActive(false);
    refresh();
}

void AudioPanel::setTestActive(bool on)
{
    const QSignalBlocker block(m_test);
    m_test->setChecked(on);
}

// ---------------------------------------------------------------- EffectsPanel

EffectsPanel::EffectsPanel(SettingsStore& store, QWidget* parent) : QWidget(parent), m_store(store)
{
    QVBoxLayout* v = pageLayout(this);

    auto* cur = new CollapsibleSection(QStringLiteral("Cursor"), this);
    m_cursor = new QCheckBox(QStringLiteral("Record the mouse cursor"), cur);
    m_highlight = new QCheckBox(QStringLiteral("Highlight the cursor"), cur);
    m_hlColor = new ColorButton;
    m_hlRadius = ui::sliderRow(QStringLiteral("Highlight size"), 10, 80, cur, [](int v) { return QStringLiteral("%1 px").arg(v); });
    m_clicks = new QCheckBox(QStringLiteral("Show clicks (rings)"), cur);
    m_leftColor = new ColorButton;
    m_rightColor = new ColorButton;
    m_clickRadius = ui::sliderRow(QStringLiteral("Click size"), 10, 80, cur, [](int v) { return QStringLiteral("%1 px").arg(v); });
    cur->body()->addWidget(m_cursor);
    cur->body()->addWidget(m_highlight);
    cur->body()->addWidget(ui::row(QStringLiteral("Highlight colour"), m_hlColor, cur));
    cur->body()->addWidget(m_hlRadius.widget);
    cur->body()->addWidget(m_clicks);
    cur->body()->addWidget(ui::row(QStringLiteral("Left click"), m_leftColor, cur));
    cur->body()->addWidget(ui::row(QStringLiteral("Right click"), m_rightColor, cur));
    cur->body()->addWidget(m_clickRadius.widget);
    v->addWidget(cur);

    auto* fx = new CollapsibleSection(QStringLiteral("Screen colour"), this, false);
    m_fx = new QCheckBox(QStringLiteral("Adjust the recorded screen image"), fx);
    m_brightness = ui::sliderRow(QStringLiteral("Brightness"), -50, 50, fx, signedValue);
    m_contrast = ui::sliderRow(QStringLiteral("Contrast"), 50, 150, fx, percent);
    m_saturation = ui::sliderRow(QStringLiteral("Saturation"), 0, 200, fx, percent);
    m_gamma = ui::sliderRow(QStringLiteral("Gamma"), 50, 200, fx, hundredths);
    m_temperature = ui::sliderRow(QStringLiteral("Temperature"), -100, 100, fx, signedValue);
    m_tint = ui::sliderRow(QStringLiteral("Tint"), -100, 100, fx, signedValue);
    m_sharpen = ui::sliderRow(QStringLiteral("Sharpen"), 0, 100, fx, percent);
    m_blur = ui::sliderRow(QStringLiteral("Blur"), 0, 16, fx, [](int v) { return QStringLiteral("%1 px").arg(v / 2.0); });
    m_gray = new QCheckBox(QStringLiteral("Black and white"), fx);
    auto* reset = new QPushButton(QStringLiteral("Reset colour"), fx);
    fx->body()->addWidget(m_fx);
    for (const ui::SliderRow* r : {&m_brightness, &m_contrast, &m_saturation, &m_gamma, &m_temperature, &m_tint,
                                   &m_sharpen, &m_blur})
        fx->body()->addWidget(r->widget);
    fx->body()->addWidget(m_gray);
    fx->body()->addWidget(reset, 0, Qt::AlignLeft);
    fx->body()->addWidget(ui::hint(QStringLiteral("Applied on the graphics card while recording; costs almost no CPU."), fx));
    v->addWidget(fx);
    v->addStretch();

    auto edit = [this](unsigned scope, auto change) {
        if (m_updating)
            return;
        m_store.edit(scope, change);
    };
    connect(m_cursor, &QCheckBox::toggled, this,
            [edit](bool on) { edit(SettingsStore::Source | SettingsStore::Effects, [on](AppSettings& s) { s.captureCursor = on; }); });
    connect(m_highlight, &QCheckBox::toggled, this,
            [edit](bool on) { edit(SettingsStore::Effects, [on](AppSettings& s) { s.cursor.highlight = on; }); });
    connect(m_hlColor, &ColorButton::colorChanged, this,
            [edit](const QColor& c) { edit(SettingsStore::Effects, [c](AppSettings& s) { s.cursor.highlightColor = toRgba(c); }); });
    connect(m_hlRadius.slider, &QSlider::valueChanged, this, [edit](int v) {
        edit(SettingsStore::Effects, [v](AppSettings& s) { s.cursor.highlightRadius = static_cast<float>(v); });
    });
    connect(m_clicks, &QCheckBox::toggled, this,
            [edit](bool on) { edit(SettingsStore::Effects, [on](AppSettings& s) { s.cursor.clicks = on; }); });
    connect(m_leftColor, &ColorButton::colorChanged, this,
            [edit](const QColor& c) { edit(SettingsStore::Effects, [c](AppSettings& s) { s.cursor.leftClickColor = toRgba(c); }); });
    connect(m_rightColor, &ColorButton::colorChanged, this,
            [edit](const QColor& c) { edit(SettingsStore::Effects, [c](AppSettings& s) { s.cursor.rightClickColor = toRgba(c); }); });
    connect(m_clickRadius.slider, &QSlider::valueChanged, this, [edit](int v) {
        edit(SettingsStore::Effects, [v](AppSettings& s) { s.cursor.clickRadius = static_cast<float>(v); });
    });
    connect(m_fx, &QCheckBox::toggled, this,
            [edit](bool on) { edit(SettingsStore::Effects, [on](AppSettings& s) { s.videoFilters.enabled = on; }); });
    // Slider value * scale -> the filter field.
    auto fxSlider = [this, edit](const ui::SliderRow& r, float scale, float gpu::VideoFilterSettings::*field) {
        connect(r.slider, &QSlider::valueChanged, this, [edit, scale, field](int v) {
            edit(SettingsStore::Effects, [v, scale, field](AppSettings& s) { s.videoFilters.*field = v * scale; });
        });
    };
    fxSlider(m_brightness, 0.01f, &gpu::VideoFilterSettings::brightness);
    fxSlider(m_contrast, 0.01f, &gpu::VideoFilterSettings::contrast);
    fxSlider(m_saturation, 0.01f, &gpu::VideoFilterSettings::saturation);
    fxSlider(m_gamma, 0.01f, &gpu::VideoFilterSettings::gamma);
    fxSlider(m_temperature, 0.01f, &gpu::VideoFilterSettings::temperature);
    fxSlider(m_tint, 0.01f, &gpu::VideoFilterSettings::tint);
    fxSlider(m_sharpen, 0.01f, &gpu::VideoFilterSettings::sharpen);
    fxSlider(m_blur, 0.5f, &gpu::VideoFilterSettings::blur);
    connect(m_gray, &QCheckBox::toggled, this,
            [edit](bool on) { edit(SettingsStore::Effects, [on](AppSettings& s) { s.videoFilters.grayscale = on; }); });
    connect(reset, &QPushButton::clicked, this, [this] {
        m_store.edit(SettingsStore::Effects, [](AppSettings& s) {
            const bool on = s.videoFilters.enabled;
            s.videoFilters = gpu::VideoFilterSettings{};
            s.videoFilters.enabled = on;
        });
    });
    connect(&m_store, &SettingsStore::changed, this, [this](unsigned scope) {
        if (scope & (SettingsStore::Effects | SettingsStore::Source))
            refresh();
    });
    refresh();
}

void EffectsPanel::refresh()
{
    ui::Updating guard(m_updating);
    const AppSettings& s = m_store.get();
    setChecked(m_cursor, s.captureCursor);
    setChecked(m_highlight, s.cursor.highlight);
    m_hlColor->setColor(toQColor(s.cursor.highlightColor));
    m_hlRadius.setValue(static_cast<int>(s.cursor.highlightRadius));
    setChecked(m_clicks, s.cursor.clicks);
    m_leftColor->setColor(toQColor(s.cursor.leftClickColor));
    m_rightColor->setColor(toQColor(s.cursor.rightClickColor));
    m_clickRadius.setValue(static_cast<int>(s.cursor.clickRadius));
    for (QWidget* w : std::initializer_list<QWidget*>{m_highlight, m_clicks})
        w->setEnabled(s.captureCursor);
    const auto& f = s.videoFilters;
    setChecked(m_fx, f.enabled);
    m_brightness.setValue(static_cast<int>(std::lround(f.brightness * 100)));
    m_contrast.setValue(static_cast<int>(std::lround(f.contrast * 100)));
    m_saturation.setValue(static_cast<int>(std::lround(f.saturation * 100)));
    m_gamma.setValue(static_cast<int>(std::lround(f.gamma * 100)));
    m_temperature.setValue(static_cast<int>(std::lround(f.temperature * 100)));
    m_tint.setValue(static_cast<int>(std::lround(f.tint * 100)));
    m_sharpen.setValue(static_cast<int>(std::lround(f.sharpen * 100)));
    m_blur.setValue(static_cast<int>(std::lround(f.blur * 2)));
    setChecked(m_gray, f.grayscale);
    for (const ui::SliderRow* r : {&m_brightness, &m_contrast, &m_saturation, &m_gamma, &m_temperature, &m_tint,
                                   &m_sharpen, &m_blur})
        r->widget->setEnabled(f.enabled);
    m_gray->setEnabled(f.enabled);
}

// --------------------------------------------------------------- OverlaysPanel

OverlaysPanel::OverlaysPanel(SettingsStore& store, WebcamController& webcam, RecordingController& recording,
                             QWidget* parent)
    : QWidget(parent), m_store(store), m_webcam(webcam), m_rec(recording)
{
    QVBoxLayout* v = pageLayout(this);

    // Webcam overlay ---------------------------------------------------------
    auto* cam = new CollapsibleSection(QStringLiteral("Webcam overlay"), this);
    m_camOn = new ToggleSwitch;
    m_camCorner = new QComboBox;
    m_camCorner->addItems({QStringLiteral("Top left"), QStringLiteral("Top right"), QStringLiteral("Bottom left"),
                           QStringLiteral("Bottom right"), QStringLiteral("Custom (layout editor)")});
    m_camSize = ui::sliderRow(QStringLiteral("Size"), 5, 60, cam, percent,
                              QStringLiteral("Width of the webcam as a share of the video width"));
    m_camOpacity = ui::sliderRow(QStringLiteral("Opacity"), 10, 100, cam, percent);
    m_camShape = new QComboBox;
    m_camShape->addItems({QStringLiteral("Rectangle"), QStringLiteral("Rounded"), QStringLiteral("Circle")});
    m_camRadius = ui::sliderRow(QStringLiteral("Corner radius"), 0, 50, cam, percent);
    m_camBorder = new QSpinBox;
    m_camBorder->setRange(0, 20);
    m_camBorder->setSuffix(QStringLiteral(" px"));
    m_camBorderColor = new ColorButton;
    m_camMirror = new QCheckBox(QStringLiteral("Mirror (like a mirror image)"), cam);
    cam->body()->addWidget(ui::row(QStringLiteral("Show webcam"), m_camOn, cam));
    cam->body()->addWidget(ui::row(QStringLiteral("Position"), m_camCorner, cam));
    cam->body()->addWidget(m_camSize.widget);
    cam->body()->addWidget(m_camOpacity.widget);
    cam->body()->addWidget(ui::row(QStringLiteral("Shape"), m_camShape, cam));
    cam->body()->addWidget(m_camRadius.widget);
    cam->body()->addWidget(ui::row(QStringLiteral("Border"), m_camBorder, cam));
    cam->body()->addWidget(ui::row(QStringLiteral("Border colour"), m_camBorderColor, cam));
    cam->body()->addWidget(m_camMirror);
    v->addWidget(cam);

    // Text and image overlays ------------------------------------------------------
    auto* items = new CollapsibleSection(QStringLiteral("Text && images"), this);
    m_list = new QListWidget(items);
    m_list->setMaximumHeight(110);
    m_list->setAccessibleName(QStringLiteral("Overlays"));
    auto* buttons = new QHBoxLayout;
    auto* addText = new QPushButton(makeIcon(IconId::Text, currentPalette().text), QStringLiteral("Add text"), items);
    auto* addImage = new QPushButton(makeIcon(IconId::Image, currentPalette().text), QStringLiteral("Add image..."), items);
    m_remove = new QPushButton(QStringLiteral("Remove"), items);
    buttons->addWidget(addText);
    buttons->addWidget(addImage);
    buttons->addStretch();
    buttons->addWidget(m_remove);
    items->body()->addWidget(m_list);
    items->body()->addLayout(buttons);

    m_editor = new QWidget(items);
    auto* ev = new QVBoxLayout(m_editor);
    ev->setContentsMargins(0, 4, 0, 0);
    ev->setSpacing(6);
    m_itemEnabled = new QCheckBox(QStringLiteral("Visible in the recording"), m_editor);
    ev->addWidget(m_itemEnabled);
    m_textFields = new QWidget(m_editor);
    auto* tv = new QVBoxLayout(m_textFields);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(6);
    m_text = new QLineEdit;
    m_font = new QFontComboBox;
    m_fontSize = new QSpinBox;
    m_fontSize->setRange(8, 200);
    m_fontSize->setSuffix(QStringLiteral(" px"));
    m_color = new ColorButton;
    m_bold = new QCheckBox(QStringLiteral("Bold"), m_textFields);
    m_outline = new QCheckBox(QStringLiteral("Outline (readable on any background)"), m_textFields);
    tv->addWidget(ui::row(QStringLiteral("Text"), m_text, m_textFields));
    tv->addWidget(ui::row(QStringLiteral("Font"), m_font, m_textFields));
    tv->addWidget(ui::row(QStringLiteral("Size"), m_fontSize, m_textFields,
                          QStringLiteral("Text height in pixels at 1080p (scaled with the video)")));
    tv->addWidget(ui::row(QStringLiteral("Colour"), m_color, m_textFields));
    tv->addWidget(m_bold);
    tv->addWidget(m_outline);
    ev->addWidget(m_textFields);
    m_imageFields = new QWidget(m_editor);
    auto* iv = new QHBoxLayout(m_imageFields);
    iv->setContentsMargins(0, 0, 0, 0);
    m_imagePath = new QLineEdit(m_imageFields);
    m_imagePath->setReadOnly(true);
    auto* browse = new QPushButton(QStringLiteral("Change..."), m_imageFields);
    iv->addWidget(m_imagePath, 1);
    iv->addWidget(browse);
    ev->addWidget(m_imageFields);
    m_x = ui::sliderRow(QStringLiteral("Horizontal"), 0, 100, m_editor, percent);
    m_y = ui::sliderRow(QStringLiteral("Vertical"), 0, 100, m_editor, percent);
    m_w = ui::sliderRow(QStringLiteral("Width"), 2, 100, m_editor, percent);
    m_opacity = ui::sliderRow(QStringLiteral("Opacity"), 5, 100, m_editor, percent);
    for (const ui::SliderRow* r : {&m_x, &m_y, &m_w, &m_opacity})
        ev->addWidget(r->widget);
    items->body()->addWidget(m_editor);
    auto* layoutEditor = new QPushButton(makeIcon(IconId::Layout, currentPalette().text),
                                         QStringLiteral("Open layout editor..."), items);
    layoutEditor->setToolTip(QStringLiteral("Drag the webcam and overlays on a preview of the frame; boxes, shadows "
                                            "and more text options"));
    items->body()->addWidget(layoutEditor, 0, Qt::AlignLeft);
    v->addWidget(items);

    // Watermark --------------------------------------------------------------
    auto* wm = new CollapsibleSection(QStringLiteral("Watermark"), this, false);
    m_wm = new QCheckBox(QStringLiteral("Show a watermark"), wm);
    m_wmText = new QLineEdit;
    m_wmCorner = new QComboBox;
    m_wmCorner->addItems({QStringLiteral("Top left"), QStringLiteral("Top right"), QStringLiteral("Bottom left"),
                          QStringLiteral("Bottom right")});
    m_wmOpacity = ui::sliderRow(QStringLiteral("Opacity"), 10, 100, wm, percent);
    wm->body()->addWidget(m_wm);
    wm->body()->addWidget(ui::row(QStringLiteral("Text"), m_wmText, wm));
    wm->body()->addWidget(ui::row(QStringLiteral("Corner"), m_wmCorner, wm));
    wm->body()->addWidget(m_wmOpacity.widget);
    v->addWidget(wm);
    v->addStretch();

    // Webcam overlay handlers
    auto editCam = [this](auto change) {
        if (m_updating)
            return;
        m_store.edit(SettingsStore::Overlays, change);
    };
    connect(m_camOn, &ToggleSwitch::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        m_store.edit(SettingsStore::Webcam | SettingsStore::Overlays, [on](AppSettings& s) { s.webcamEnabled = on; });
    });
    connect(m_camCorner, &QComboBox::activated, this, [this](int i) {
        if (!m_updating && i < 4)
            placeWebcam(i);
    });
    connect(m_camSize.slider, &QSlider::valueChanged, this, [this, editCam](int v) {
        const int corner = m_camCorner->currentIndex();
        editCam([v](AppSettings& s) { s.webcamPlacement.w = v / 100.f; });
        if (!m_updating && corner < 4)
            placeWebcam(corner); // keep it in its corner while resizing
    });
    connect(m_camOpacity.slider, &QSlider::valueChanged, this,
            [editCam](int v) { editCam([v](AppSettings& s) { s.webcamPlacement.opacity = v / 100.f; }); });
    connect(m_camShape, &QComboBox::activated, this, [editCam](int i) {
        editCam([i](AppSettings& s) { s.webcamPlacement.shape = static_cast<gpu::WebcamShape>(i); });
    });
    connect(m_camRadius.slider, &QSlider::valueChanged, this,
            [editCam](int v) { editCam([v](AppSettings& s) { s.webcamPlacement.cornerRadius = v / 100.f; }); });
    connect(m_camBorder, &QSpinBox::valueChanged, this,
            [editCam](int v) { editCam([v](AppSettings& s) { s.webcamPlacement.borderPx = static_cast<float>(v); }); });
    connect(m_camBorderColor, &ColorButton::colorChanged, this,
            [editCam](const QColor& c) { editCam([c](AppSettings& s) { s.webcamPlacement.borderColor = toRgba(c); }); });
    connect(m_camMirror, &QCheckBox::toggled, this,
            [editCam](bool on) { editCam([on](AppSettings& s) { s.webcamPlacement.mirror = on; }); });

    // Overlay items
    connect(m_list, &QListWidget::currentRowChanged, this, [this] { loadItem(); });
    connect(addText, &QPushButton::clicked, this, [this] {
        m_store.edit(SettingsStore::Overlays, [](AppSettings& s) {
            OverlayItem t;
            t.type = OverlayItem::Type::Text;
            t.name = QStringLiteral("Text");
            t.text = QStringLiteral("Your text");
            t.x = 0.05;
            t.y = 0.05;
            t.w = 0.4;
            t.h = 0.07;
            s.overlays.append(t);
        });
        m_list->setCurrentRow(m_list->count() - 1);
    });
    connect(addImage, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Choose an image"), QString(),
                                                          QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));
        if (file.isEmpty())
            return;
        m_store.edit(SettingsStore::Overlays, [file](AppSettings& s) {
            OverlayItem img;
            img.type = OverlayItem::Type::Image;
            img.name = QFileInfo(file).fileName();
            img.imagePath = file;
            img.x = 0.76;
            img.y = 0.04;
            img.w = 0.2;
            img.h = 0.2;
            s.overlays.append(img);
        });
        m_list->setCurrentRow(m_list->count() - 1);
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const int i = currentItem();
        if (i < 0)
            return;
        m_store.edit(SettingsStore::Overlays, [i](AppSettings& s) {
            if (i < s.overlays.size())
                s.overlays.removeAt(i);
        });
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Choose an image"), m_imagePath->text(),
                                                          QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));
        if (!file.isEmpty()) {
            m_imagePath->setText(file);
            storeItem();
        }
    });
    for (QCheckBox* c : {m_itemEnabled, m_bold, m_outline})
        connect(c, &QCheckBox::toggled, this, [this] { storeItem(); });
    connect(m_text, &QLineEdit::editingFinished, this, [this] { storeItem(); });
    connect(m_font, &QFontComboBox::currentFontChanged, this, [this] { storeItem(); });
    connect(m_fontSize, &QSpinBox::valueChanged, this, [this] { storeItem(); });
    connect(m_color, &ColorButton::colorChanged, this, [this] { storeItem(); });
    for (const ui::SliderRow* r : {&m_x, &m_y, &m_w, &m_opacity})
        connect(r->slider, &QSlider::valueChanged, this, [this] { storeItem(); });
    connect(layoutEditor, &QPushButton::clicked, this, &OverlaysPanel::openLayoutEditor);

    // Watermark
    auto editWm = [this](auto change) {
        if (m_updating)
            return;
        m_store.edit(SettingsStore::Overlays, change);
    };
    connect(m_wm, &QCheckBox::toggled, this, [editWm](bool on) { editWm([on](AppSettings& s) { s.watermark.enabled = on; }); });
    connect(m_wmText, &QLineEdit::editingFinished, this, [this, editWm] {
        const QString t = m_wmText->text();
        editWm([t](AppSettings& s) { s.watermark.text = t; });
    });
    connect(m_wmCorner, &QComboBox::activated, this, [editWm](int i) { editWm([i](AppSettings& s) { s.watermark.corner = i; }); });
    connect(m_wmOpacity.slider, &QSlider::valueChanged, this,
            [editWm](int v) { editWm([v](AppSettings& s) { s.watermark.opacity = v / 100.0; }); });

    connect(&m_store, &SettingsStore::changed, this, [this](unsigned scope) {
        if (scope & (SettingsStore::Overlays | SettingsStore::Webcam | SettingsStore::Source | SettingsStore::Video))
            refresh();
    });
    refresh();
}

int OverlaysPanel::currentItem() const
{
    const int i = m_list->currentRow();
    return i >= 0 && i < m_store.get().overlays.size() ? i : -1;
}

void OverlaysPanel::placeWebcam(int corner)
{
    QSize out = m_rec.outputSize();
    if (!out.isValid() || out.isEmpty())
        out = QSize(1920, 1080);
    m_store.edit(SettingsStore::Overlays, [&](AppSettings& s) {
        gpu::WebcamPlacement& p = s.webcamPlacement;
        const gpu::WebcamPlacement r = resolvedPlacement(p, m_webcam.aspect(p), out.width(), out.height());
        const float mx = 0.02f, my = 0.02f * static_cast<float>(out.width()) / static_cast<float>(out.height());
        p.x = (corner == 1 || corner == 3) ? 1.f - r.w - mx : mx;
        p.y = corner >= 2 ? 1.f - r.h - my : my;
    });
}

void OverlaysPanel::refreshList()
{
    const QSignalBlocker block(m_list);
    const int row = m_list->currentRow();
    m_list->clear();
    for (const OverlayItem& o : m_store.get().overlays) {
        QString label;
        IconId icon = IconId::Layers;
        switch (o.type) {
        case OverlayItem::Type::Text:
            label = o.text.simplified().left(40);
            icon = IconId::Text;
            break;
        case OverlayItem::Type::Image:
            label = QFileInfo(o.imagePath).fileName();
            icon = IconId::Image;
            break;
        case OverlayItem::Type::Rectangle:
            label = QStringLiteral("Box");
            break;
        }
        auto* it = new QListWidgetItem(makeIcon(icon, o.enabled ? currentPalette().text : currentPalette().textDim),
                                       label.isEmpty() ? QStringLiteral("(empty)") : label, m_list);
        it->setToolTip(o.enabled ? QString() : QStringLiteral("Hidden"));
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(std::clamp(row, 0, m_list->count() - 1));
}

void OverlaysPanel::refresh()
{
    {
        ui::Updating guard(m_updating);
        const AppSettings& s = m_store.get();
        const gpu::WebcamPlacement& p = s.webcamPlacement;
        {
            const QSignalBlocker b(m_camOn);
            m_camOn->setChecked(s.webcamEnabled);
        }
        QSize out = m_rec.outputSize();
        if (!out.isValid() || out.isEmpty())
            out = QSize(1920, 1080);
        const gpu::WebcamPlacement r = resolvedPlacement(p, m_webcam.aspect(p), out.width(), out.height());
        const float mx = 0.02f, my = 0.02f * static_cast<float>(out.width()) / static_cast<float>(out.height());
        int corner = 4;
        for (int c = 0; c < 4; ++c) {
            const float x = (c == 1 || c == 3) ? 1.f - r.w - mx : mx;
            const float y = c >= 2 ? 1.f - r.h - my : my;
            if (std::abs(x - r.x) < 0.01f && std::abs(y - r.y) < 0.01f)
                corner = c;
        }
        {
            const QSignalBlocker b(m_camCorner);
            m_camCorner->setCurrentIndex(corner);
        }
        m_camSize.setValue(static_cast<int>(std::lround(p.w * 100)));
        m_camOpacity.setValue(static_cast<int>(std::lround(p.opacity * 100)));
        {
            const QSignalBlocker b(m_camShape);
            m_camShape->setCurrentIndex(static_cast<int>(p.shape));
        }
        m_camRadius.setValue(static_cast<int>(std::lround(p.cornerRadius * 100)));
        m_camRadius.widget->setEnabled(p.shape == gpu::WebcamShape::Rounded);
        setSpin(m_camBorder, static_cast<int>(p.borderPx));
        m_camBorderColor->setColor(toQColor(p.borderColor));
        setChecked(m_camMirror, p.mirror);

        setChecked(m_wm, s.watermark.enabled);
        {
            const QSignalBlocker b(m_wmText);
            m_wmText->setText(s.watermark.text);
        }
        {
            const QSignalBlocker b(m_wmCorner);
            m_wmCorner->setCurrentIndex(std::clamp(s.watermark.corner, 0, 3));
        }
        m_wmOpacity.setValue(static_cast<int>(std::lround(s.watermark.opacity * 100)));
        refreshList();
    }
    loadItem();
}

void OverlaysPanel::loadItem()
{
    ui::Updating guard(m_updating);
    const int i = currentItem();
    m_editor->setVisible(i >= 0);
    m_remove->setEnabled(i >= 0);
    if (i < 0)
        return;
    const OverlayItem& o = m_store.get().overlays[i];
    setChecked(m_itemEnabled, o.enabled);
    m_textFields->setVisible(o.type == OverlayItem::Type::Text);
    m_imageFields->setVisible(o.type == OverlayItem::Type::Image);
    {
        const QSignalBlocker b1(m_text), b2(m_font), b3(m_fontSize), b4(m_imagePath);
        m_text->setText(o.text);
        m_font->setCurrentFont(QFont(o.fontFamily));
        m_fontSize->setValue(o.fontSize);
        m_imagePath->setText(o.imagePath);
    }
    m_color->setColor(o.color);
    setChecked(m_bold, o.bold);
    setChecked(m_outline, o.outline);
    m_x.setValue(static_cast<int>(std::lround(o.x * 100)));
    m_y.setValue(static_cast<int>(std::lround(o.y * 100)));
    m_w.setValue(static_cast<int>(std::lround(o.w * 100)));
    m_opacity.setValue(static_cast<int>(std::lround(o.opacity * 100)));
}

void OverlaysPanel::storeItem()
{
    if (m_updating)
        return;
    const int i = currentItem();
    if (i < 0)
        return;
    const bool enabled = m_itemEnabled->isChecked();
    const QString text = m_text->text();
    const QString family = m_font->currentFont().family();
    const int size = m_fontSize->value();
    const QColor color = m_color->color();
    const bool bold = m_bold->isChecked(), outline = m_outline->isChecked();
    const QString image = m_imagePath->text();
    const double x = m_x.slider->value() / 100.0, y = m_y.slider->value() / 100.0;
    const double w = m_w.slider->value() / 100.0, opacity = m_opacity.slider->value() / 100.0;
    m_store.edit(SettingsStore::Overlays, [=](AppSettings& s) {
        if (i >= s.overlays.size())
            return;
        OverlayItem& o = s.overlays[i];
        o.enabled = enabled;
        o.x = std::clamp(x, 0.0, 0.98);
        o.y = std::clamp(y, 0.0, 0.98);
        o.w = std::clamp(w, 0.02, 1.0 - o.x);
        o.opacity = opacity;
        if (o.type == OverlayItem::Type::Text) {
            o.text = text;
            o.fontFamily = family;
            o.fontSize = size;
            o.color = color;
            o.bold = bold;
            o.outline = outline;
            const int lines = static_cast<int>(text.count(QLatin1Char('\n'))) + 1;
            o.h = std::min(1.0 - o.y, size * 1.5 * lines / 1080.0); // room for the text at its size
        } else if (o.type == OverlayItem::Type::Image) {
            o.imagePath = image;
            o.name = QFileInfo(image).fileName();
            o.h = std::min(1.0 - o.y, o.w * 16.0 / 9.0); // drawn aspect-fit inside the box
        }
    });
}

} // namespace luma::app
