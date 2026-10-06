#include "ui/RecordPage.h"

#include "History.h"
#include "Icons.h"
#include "PreviewController.h"
#include "RecordingController.h"
#include "SettingsStore.h"
#include "Theme.h"
#include "ThumbnailCache.h"
#include "WebcamController.h"
#include "WinUtil.h"
#include "ui/ContextPanels.h"
#include "ui/RecordingDelegate.h"
#include "ui/UiKit.h"
#include "ui/WebcamPanel.h"
#include "widgets/Widgets.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace luma::app {
namespace {

QString deviceName(const QList<AudioEntry>& list, const QString& id)
{
    if (id.isEmpty()) {
        for (const AudioEntry& e : list)
            if (e.isDefault)
                return QStringLiteral("Default (%1)").arg(e.name);
        return QStringLiteral("Windows default device");
    }
    for (const AudioEntry& e : list)
        if (e.id == id)
            return e.name;
    return QStringLiteral("Selected device (not connected)");
}

QScrollArea* scrolled(QWidget* content, QWidget* parent)
{
    auto* sa = new QScrollArea(parent);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sa->setWidget(content);
    return sa;
}

// Icon + title + subtitle (+ optional toggle and settings button) for the Sources list.
QWidget* sourceRow(IconId icon, const QString& title, QLabel*& subtitle, ToggleSwitch* toggle, QToolButton* gear,
                   QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* g = new QGridLayout(w);
    g->setContentsMargins(2, 2, 2, 2);
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(0);
    auto* ic = new QLabel(w);
    ic->setPixmap(makeIcon(icon, currentPalette().text).pixmap(20, 20));
    auto* t = new QLabel(title, w);
    subtitle = new QLabel(w);
    subtitle->setObjectName("Hint");
    g->addWidget(ic, 0, 0, 2, 1);
    g->addWidget(t, 0, 1);
    g->addWidget(subtitle, 1, 1);
    g->setColumnStretch(1, 1);
    if (gear) {
        gear->setParent(w);
        g->addWidget(gear, 0, 2, 2, 1);
    }
    if (toggle) {
        toggle->setParent(w);
        toggle->setAccessibleName(title);
        g->addWidget(toggle, 0, 3, 2, 1);
    }
    return w;
}

QToolButton* gearButton(const QString& tip)
{
    auto* b = new QToolButton;
    b->setIcon(makeIcon(IconId::Settings, currentPalette().textDim));
    b->setToolTip(tip);
    b->setAccessibleName(tip);
    return b;
}

} // namespace

RecordPage::RecordPage(SettingsStore& store, DeviceScanner& scanner, WebcamController& webcam,
                       RecordingController& recording, PreviewController& preview, History& history,
                       ThumbnailCache& thumbs, QWidget* parent)
    : QWidget(parent), m_store(store), m_scanner(scanner), m_webcam(webcam), m_rec(recording), m_preview(preview),
      m_history(history), m_thumbs(thumbs)
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 10);
    root->setSpacing(12);
    root->addWidget(buildLeft());
    root->addWidget(buildCenter(), 1);
    root->addWidget(buildRight());

    connect(&m_store, &SettingsStore::changed, this, &RecordPage::refreshFromSettings);
    connect(&m_scanner, &DeviceScanner::monitorsReady, this, &RecordPage::fillMonitors);
    connect(&m_scanner, &DeviceScanner::windowsReady, this, &RecordPage::fillWindows);
    connect(&m_scanner, &DeviceScanner::audioReady, this, [this] { refreshFromSettings(SettingsStore::Audio); });
    connect(&m_scanner, &DeviceScanner::camerasReady, this, [this] { refreshFromSettings(SettingsStore::Webcam); });
    connect(&m_thumbs, &ThumbnailCache::thumbnailReady, this, [this] { m_recent->viewport()->update(); });
    refreshFromSettings(SettingsStore::All);
    refreshRecent();
    updateTransport();
}

// ------------------------------------------------------------------- left

QWidget* RecordPage::buildLeft()
{
    m_leftContent = new QWidget;
    auto* v = new QVBoxLayout(m_leftContent);
    v->setContentsMargins(0, 0, 6, 0);
    v->setSpacing(12);
    v->addWidget(buildCaptureMode(m_leftContent));
    v->addWidget(buildSources(m_leftContent));
    v->addWidget(buildOutput(m_leftContent));
    v->addWidget(buildQuickSettings(m_leftContent));
    v->addStretch();
    QScrollArea* sa = scrolled(m_leftContent, this);
    sa->setFixedWidth(300);
    return sa;
}

QWidget* RecordPage::buildCaptureMode(QWidget* parent)
{
    QVBoxLayout* body = nullptr;
    QFrame* f = ui::panel(parent, body, QStringLiteral("Capture Mode"));
    body->setSpacing(4);
    m_modeGroup = new QButtonGroup(f);
    m_modeGroup->setExclusive(true);
    struct Mode {
        SourceKind kind;
        IconId icon;
        const char* title;
        const char* subtitle;
    };
    const Mode modes[] = {
        {SourceKind::Display, IconId::Display, "Display", "Record your full screen"},
        {SourceKind::Window, IconId::Window, "Window", "Record a single window"},
        {SourceKind::Region, IconId::Region, "Region", "Record a selected area"},
        {SourceKind::Game, IconId::Game, "Game (Beta)", "Records the monitor your game is on"},
    };
    for (const Mode& m : modes) {
        auto* b = new ModeButton(m.icon, QString::fromLatin1(m.title), QString::fromLatin1(m.subtitle), f);
        m_modeGroup->addButton(b, static_cast<int>(m.kind));
        body->addWidget(b);
    }
    m_modeGroup->button(static_cast<int>(SourceKind::Game))
        ->setToolTip(QStringLiteral("Beta: records the whole monitor the chosen game window is on, with Desktop "
                                    "Duplication. Works for full-screen and borderless games and shows no capture "
                                    "border. Anything else on that monitor is recorded too."));
    m_modeGroup->button(static_cast<int>(SourceKind::Window))
        ->setToolTip(QStringLiteral("Records one window even when it is covered (Windows.Graphics.Capture). On Windows "
                                    "10, Windows draws a yellow border around the captured window."));

    m_targetStack = new QStackedWidget(f);
    // Display
    auto* dp = new QWidget(m_targetStack);
    auto* dl = new QVBoxLayout(dp);
    dl->setContentsMargins(0, 6, 0, 0);
    m_display = new QComboBox(dp);
    m_display->setAccessibleName(QStringLiteral("Display"));
    dl->addWidget(m_display);
    m_targetStack->addWidget(dp);
    // Window
    auto* wp = new QWidget(m_targetStack);
    auto* wl = new QHBoxLayout(wp);
    wl->setContentsMargins(0, 6, 0, 0);
    m_window = new QComboBox(wp);
    m_window->setAccessibleName(QStringLiteral("Window"));
    m_window->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_window->setMinimumContentsLength(14);
    auto* refreshW = new QToolButton(wp);
    refreshW->setIcon(makeIcon(IconId::Refresh, currentPalette().text));
    refreshW->setToolTip(QStringLiteral("Refresh the window list"));
    refreshW->setAccessibleName(QStringLiteral("Refresh window list"));
    wl->addWidget(m_window, 1);
    wl->addWidget(refreshW);
    m_targetStack->addWidget(wp);
    // Region
    auto* rp = new QWidget(m_targetStack);
    auto* rl = new QGridLayout(rp);
    rl->setContentsMargins(0, 6, 0, 0);
    auto* pick = new QPushButton(makeIcon(IconId::Region, currentPalette().text), QStringLiteral("Select area..."), rp);
    m_regionAspect = new QComboBox(rp);
    m_regionAspect->addItem(QStringLiteral("Free"), 0.0);
    m_regionAspect->addItem(QStringLiteral("16:9"), 16.0 / 9.0);
    m_regionAspect->addItem(QStringLiteral("4:3"), 4.0 / 3.0);
    m_regionAspect->addItem(QStringLiteral("1:1"), 1.0);
    m_regionAspect->addItem(QStringLiteral("9:16"), 9.0 / 16.0);
    m_regionAspect->setToolTip(QStringLiteral("Lock the aspect ratio while dragging"));
    m_regionLabel = ui::hint(QString(), rp);
    rl->addWidget(pick, 0, 0);
    rl->addWidget(m_regionAspect, 0, 1);
    rl->addWidget(m_regionLabel, 1, 0, 1, 2);
    m_targetStack->addWidget(rp);
    // Game
    auto* gp = new QWidget(m_targetStack);
    auto* gl = new QGridLayout(gp);
    gl->setContentsMargins(0, 6, 0, 0);
    m_game = new QComboBox(gp);
    m_game->setAccessibleName(QStringLiteral("Game window"));
    m_game->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_game->setMinimumContentsLength(14);
    auto* refreshG = new QToolButton(gp);
    refreshG->setIcon(makeIcon(IconId::Refresh, currentPalette().text));
    refreshG->setToolTip(QStringLiteral("Refresh the window list"));
    gl->addWidget(m_game, 0, 0);
    gl->addWidget(refreshG, 0, 1);
    gl->addWidget(ui::hint(QStringLiteral("Start the game first, then choose its window. The whole monitor it is on is "
                                          "recorded."),
                           gp),
                  1, 0, 1, 2);
    gl->setColumnStretch(0, 1);
    m_targetStack->addWidget(gp);
    body->addWidget(m_targetStack);
    // A QStackedWidget is as tall as its tallest page; let only the current page count.
    auto fitStack = [this](int index) {
        for (int i = 0; i < m_targetStack->count(); ++i)
            m_targetStack->widget(i)->setSizePolicy(QSizePolicy::Preferred,
                                                    i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        m_targetStack->adjustSize();
    };
    connect(m_targetStack, &QStackedWidget::currentChanged, this, fitStack);
    fitStack(0);

    m_sourceWarning = new QLabel(f);
    m_sourceWarning->setObjectName("Warn");
    m_sourceWarning->setWordWrap(true);
    m_sourceWarning->hide();
    body->addWidget(m_sourceWarning);

    connect(m_modeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        if (m_updating)
            return;
        const auto kind = static_cast<SourceKind>(id);
        m_store.edit(SettingsStore::Source, [kind](AppSettings& s) { s.sourceKind = kind; });
        if (kind == SourceKind::Window || kind == SourceKind::Game)
            m_scanner.scanWindows();
    });
    connect(m_display, &QComboBox::activated, this, [this](int i) {
        const QString name = m_display->itemData(i).toString();
        m_store.edit(SettingsStore::Source, [name](AppSettings& s) { s.displayName = name; });
    });
    connect(m_window, &QComboBox::activated, this, [this](int i) {
        m_rec.setWindowTarget(m_window->itemData(i).value<quintptr>());
        const QString title = m_window->itemData(i, Qt::ToolTipRole).toString();
        m_store.edit(SettingsStore::Source, [title](AppSettings& s) { s.windowTitle = title; });
    });
    connect(m_game, &QComboBox::activated, this, [this](int i) {
        m_rec.setGameTarget(m_game->itemData(i).value<quintptr>());
        const QString title = m_game->itemData(i, Qt::ToolTipRole).toString();
        m_store.edit(SettingsStore::Source, [title](AppSettings& s) { s.gameTitle = title; });
    });
    connect(refreshW, &QToolButton::clicked, &m_scanner, [this] { m_scanner.scanWindows(); });
    connect(refreshG, &QToolButton::clicked, &m_scanner, [this] { m_scanner.scanWindows(); });
    connect(pick, &QPushButton::clicked, this,
            [this] { emit selectRegionRequested(m_regionAspect->currentData().toDouble()); });
    return f;
}

QWidget* RecordPage::buildSources(QWidget* parent)
{
    QVBoxLayout* body = nullptr;
    QFrame* f = ui::panel(parent, body, QStringLiteral("Sources"));
    body->setSpacing(6);
    body->addWidget(sourceRow(IconId::Display, QStringLiteral("Screen"), m_srcDisplayInfo, nullptr, nullptr, f));
    m_srcDisplayName = m_srcDisplayInfo; // one subtitle line shows the source and its size

    m_srcWebcam = new ToggleSwitch;
    QToolButton* camGear = gearButton(QStringLiteral("Webcam settings"));
    body->addWidget(sourceRow(IconId::Camera, QStringLiteral("Webcam"), m_srcWebcamName, m_srcWebcam, camGear, f));
    m_srcMic = new ToggleSwitch;
    QToolButton* micGear = gearButton(QStringLiteral("Microphone settings"));
    body->addWidget(sourceRow(IconId::Mic, QStringLiteral("Microphone"), m_srcMicName, m_srcMic, micGear, f));
    m_srcSystem = new ToggleSwitch;
    QToolButton* sysGear = gearButton(QStringLiteral("System audio settings"));
    body->addWidget(sourceRow(IconId::Speaker, QStringLiteral("System Audio"), m_srcSystemName, m_srcSystem, sysGear, f));
    auto* refresh = new QPushButton(makeIcon(IconId::Refresh, currentPalette().text), QStringLiteral("Refresh devices"), f);
    refresh->setToolTip(QStringLiteral("Look again for displays, cameras, microphones and speakers"));
    body->addWidget(refresh, 0, Qt::AlignLeft);

    connect(m_srcWebcam, &ToggleSwitch::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        if (on && m_store.get().webcamLink.isEmpty()) {
            if (m_scanner.cameras().isEmpty()) {
                emit notify(2, QStringLiteral("No camera found. Connect a webcam, then press Refresh devices."));
                ui::Updating guard(m_updating);
                const QSignalBlocker block(m_srcWebcam);
                m_srcWebcam->setChecked(false);
                return;
            }
            const CameraEntry cam = m_scanner.cameras().first();
            m_store.edit(SettingsStore::Webcam, [cam](AppSettings& s) {
                s.webcamLink = cam.link;
                s.webcamName = cam.name;
            });
        }
        m_store.edit(SettingsStore::Webcam | SettingsStore::Overlays, [on](AppSettings& s) { s.webcamEnabled = on; });
    });
    connect(m_srcMic, &ToggleSwitch::toggled, this, [this](bool on) {
        if (!m_updating)
            m_store.edit(SettingsStore::Audio, [on](AppSettings& s) { s.microphone = on; });
    });
    connect(m_srcSystem, &ToggleSwitch::toggled, this, [this](bool on) {
        if (!m_updating)
            m_store.edit(SettingsStore::Audio, [on](AppSettings& s) { s.systemAudio = on; });
    });
    connect(camGear, &QToolButton::clicked, this, [this] { showContextTab(WebcamTab); });
    connect(micGear, &QToolButton::clicked, this, [this] { showContextTab(AudioTab); });
    connect(sysGear, &QToolButton::clicked, this, [this] { showContextTab(AudioTab); });
    connect(refresh, &QPushButton::clicked, this, &RecordPage::refreshDevicesRequested);
    return f;
}

QWidget* RecordPage::buildOutput(QWidget* parent)
{
    QVBoxLayout* body = nullptr;
    QFrame* f = ui::panel(parent, body, QStringLiteral("Output"));
    auto* pathRow = new QWidget(f);
    auto* ph = new QHBoxLayout(pathRow);
    ph->setContentsMargins(0, 0, 0, 0);
    ph->setSpacing(6);
    m_outputDir = new QLineEdit(pathRow);
    m_outputDir->setReadOnly(true);
    m_outputDir->setAccessibleName(QStringLiteral("Recordings folder"));
    auto* choose = new QToolButton(pathRow);
    choose->setIcon(makeIcon(IconId::Folder, currentPalette().text));
    choose->setToolTip(QStringLiteral("Choose the recordings folder"));
    choose->setAccessibleName(QStringLiteral("Choose recordings folder"));
    ph->addWidget(m_outputDir, 1);
    ph->addWidget(choose);
    body->addWidget(ui::row(QStringLiteral("Save to"), pathRow, f));
    m_namePattern = new QLineEdit;
    body->addWidget(ui::row(QStringLiteral("File name"), m_namePattern, f,
                            QStringLiteral("Placeholders: {date} {time} {res} {fps} {source}")));
    m_container = new QComboBox;
    m_container->addItem(QStringLiteral("MKV (recommended)"), 0);
    m_container->addItem(QStringLiteral("MP4"), 1);
    body->addWidget(ui::row(QStringLiteral("Container"), m_container, f,
                            QStringLiteral("MKV survives crashes and power loss. MP4 is made from the MKV after "
                                           "recording.")));
    connect(choose, &QToolButton::clicked, this, &RecordPage::chooseOutputFolder);
    connect(m_namePattern, &QLineEdit::editingFinished, this, [this] {
        const QString pattern = m_namePattern->text().trimmed();
        if (pattern != m_store.get().namePattern)
            m_store.edit(SettingsStore::Output, [pattern](AppSettings& s) {
                s.namePattern = pattern.isEmpty() ? QStringLiteral("LumaCapture_{date}_{time}") : pattern;
            });
    });
    connect(m_container, &QComboBox::activated, this, [this](int i) {
        const int c = m_container->itemData(i).toInt();
        m_store.edit(SettingsStore::Output, [c](AppSettings& s) { s.container = static_cast<Container>(c); });
    });
    return f;
}

QWidget* RecordPage::buildQuickSettings(QWidget* parent)
{
    QVBoxLayout* body = nullptr;
    QFrame* f = ui::panel(parent, body, QStringLiteral("Quick Settings"));
    m_profile = new QComboBox;
    m_profile->addItems(profileNames());
    m_profile->addItem(QStringLiteral("Custom"));
    m_qsResolution = new QComboBox;
    fillResolutionCombo(m_qsResolution);
    m_qsFps = new QComboBox;
    fillFpsCombo(m_qsFps);
    m_qsQuality = new QComboBox;
    fillQualityCombo(m_qsQuality);
    auto* encoder = new QLabel(QStringLiteral("H.264 (x264, CPU)"));
    body->addWidget(ui::row(QStringLiteral("Profile"), m_profile, f,
                            QStringLiteral("Sets several options at once; you can still change each of them")));
    body->addWidget(ui::row(QStringLiteral("Resolution"), m_qsResolution, f));
    body->addWidget(ui::row(QStringLiteral("Frame rate"), m_qsFps, f));
    body->addWidget(ui::row(QStringLiteral("Quality"), m_qsQuality, f));
    body->addWidget(ui::row(QStringLiteral("Encoder"), encoder, f));
    m_qsPerf = new QLabel(f);
    m_qsPerf->setTextFormat(Qt::RichText);
    m_qsPerf->setWordWrap(true);
    body->addWidget(m_qsPerf);
    auto* advanced = new QPushButton(makeIcon(IconId::Settings, currentPalette().text), QStringLiteral("Advanced settings"), f);
    body->addWidget(advanced);

    connect(m_profile, &QComboBox::activated, this, [this](int i) {
        const QString name = m_profile->itemText(i);
        if (!profileNames().contains(name))
            return;
        m_store.edit(SettingsStore::All, [name](AppSettings& s) { applyProfile(s, name); });
        emit notify(0, QStringLiteral("Profile \"%1\": %2").arg(name, profileDescription(name)));
    });
    auto videoEdit = [this](auto change) {
        if (m_updating)
            return;
        m_store.edit(SettingsStore::Video, [&change](AppSettings& s) {
            change(s);
            s.profile.clear();
        });
    };
    connect(m_qsResolution, &QComboBox::activated, this, [this, videoEdit](int i) {
        const int r = m_qsResolution->itemData(i).toInt();
        videoEdit([r](AppSettings& s) { s.resolution = static_cast<ResolutionPreset>(r); });
    });
    connect(m_qsFps, &QComboBox::activated, this, [this, videoEdit](int i) {
        const int fps = m_qsFps->itemData(i).toInt();
        videoEdit([fps](AppSettings& s) { s.fps = fps; });
    });
    connect(m_qsQuality, &QComboBox::activated, this, [this, videoEdit](int i) {
        const int q = m_qsQuality->itemData(i).toInt();
        videoEdit([q](AppSettings& s) { s.quality = static_cast<QualityPreset>(q); });
    });
    connect(advanced, &QPushButton::clicked, this, [this] { showContextTab(VideoTab, true); });
    return f;
}

// ----------------------------------------------------------------- centre

QWidget* RecordPage::buildCenter()
{
    auto* center = new QWidget(this);
    auto* v = new QVBoxLayout(center);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(12);

    auto* previewPanel = new QFrame(center);
    previewPanel->setObjectName("Panel");
    auto* pv = new QVBoxLayout(previewPanel);
    pv->setContentsMargins(1, 1, 1, 8);
    pv->setSpacing(6);
    m_previewView = new PreviewView(previewPanel);
    pv->addWidget(m_previewView, 1);
    auto* strip = new QHBoxLayout;
    strip->setContentsMargins(10, 0, 10, 0);
    m_stateText = new QLabel(QStringLiteral("Ready to record"), previewPanel);
    m_stateText->setObjectName("StateText");
    m_summary = ui::dim(QString(), previewPanel);
    m_summary->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    strip->addWidget(m_stateText);
    strip->addStretch();
    strip->addWidget(m_summary);
    pv->addLayout(strip);
    v->addWidget(previewPanel, 1);

    v->addWidget(buildTransport(center));
    v->addWidget(buildDiagnostics(center));
    v->addWidget(buildRecent(center));

    connect(&m_preview, &PreviewController::frameReady, m_previewView, &PreviewView::setFrame);
    connect(&m_preview, &PreviewController::messageChanged, m_previewView, &PreviewView::setMessage);
    return center;
}

QWidget* RecordPage::buildTransport(QWidget* parent)
{
    auto* f = new QFrame(parent);
    f->setObjectName("Panel");
    auto* h = new QHBoxLayout(f);
    h->setContentsMargins(16, 10, 16, 10);
    h->setSpacing(14);

    auto* timeBox = new QVBoxLayout;
    timeBox->setSpacing(0);
    m_timer = new QLabel(QStringLiteral("00:00:00"), f);
    m_timer->setObjectName("Timer");
    m_timer->setAccessibleName(QStringLiteral("Recording time"));
    m_timerState = ui::dim(QStringLiteral("Ready"), f);
    timeBox->addStretch();
    timeBox->addWidget(m_timer);
    timeBox->addWidget(m_timerState);
    timeBox->addStretch();
    h->addLayout(timeBox);
    h->addSpacing(10);

    m_record = new TransportButton(QStringLiteral("Record"), 60, f);
    m_record->setToolTip(QStringLiteral("Start recording (Ctrl+Alt+R)"));
    m_pause = new TransportButton(QStringLiteral("Pause"), 46, f);
    m_pause->setToolTip(QStringLiteral("Pause / resume (Ctrl+Alt+P)"));
    m_stop = new TransportButton(QStringLiteral("Stop"), 46, f);
    m_stop->setToolTip(QStringLiteral("Stop and save (Ctrl+Alt+R or Ctrl+Alt+S)"));
    m_screenshot = new TransportButton(QStringLiteral("Screenshot"), 46, f);
    m_screenshot->setToolTip(QStringLiteral("Save a PNG of the selected source (Ctrl+Alt+X). LumaCapture's own windows "
                                            "are not included."));
    m_screenshot->setVisual(IconId::Screenshot, currentPalette().surfaceAlt, currentPalette().text);
    for (TransportButton* b : {m_record, m_pause, m_stop, m_screenshot})
        h->addWidget(b, 0, Qt::AlignVCenter);
    h->addStretch();

    auto* meters = new QGridLayout;
    meters->setHorizontalSpacing(8);
    meters->setVerticalSpacing(6);
    auto* sysIcon = new QLabel(f);
    sysIcon->setPixmap(makeIcon(IconId::Speaker, currentPalette().textDim).pixmap(16, 16));
    auto* micIcon = new QLabel(f);
    micIcon->setPixmap(makeIcon(IconId::Mic, currentPalette().textDim).pixmap(16, 16));
    m_systemMeter = new LevelMeter(f);
    m_systemMeter->setMinimumWidth(150);
    m_systemMeter->setAccessibleName(QStringLiteral("System audio level"));
    m_micMeter = new LevelMeter(f);
    m_micMeter->setMinimumWidth(150);
    m_micMeter->setAccessibleName(QStringLiteral("Microphone level"));
    m_micMute = new QToolButton(f);
    m_micMute->setCheckable(true);
    m_micMute->setIcon(makeIcon(IconId::Mic, currentPalette().text));
    m_micMute->setToolTip(QStringLiteral("Mute / unmute the microphone (Ctrl+Alt+M)"));
    m_micMute->setAccessibleName(QStringLiteral("Mute microphone"));
    m_testLevels = new QPushButton(QStringLiteral("Test"), f);
    m_testLevels->setCheckable(true);
    m_testLevels->setToolTip(QStringLiteral("Show the levels without recording (opens the devices; nothing is saved)"));
    meters->addWidget(sysIcon, 0, 0);
    meters->addWidget(ui::dim(QStringLiteral("System audio"), f), 0, 1);
    meters->addWidget(m_systemMeter, 0, 2);
    meters->addWidget(m_testLevels, 0, 3);
    meters->addWidget(micIcon, 1, 0);
    meters->addWidget(ui::dim(QStringLiteral("Microphone"), f), 1, 1);
    meters->addWidget(m_micMeter, 1, 2);
    meters->addWidget(m_micMute, 1, 3);
    h->addLayout(meters);

    connect(m_record, &TransportButton::clicked, this, &RecordPage::recordClicked);
    connect(m_pause, &TransportButton::clicked, this, &RecordPage::pauseClicked);
    connect(m_stop, &TransportButton::clicked, this, &RecordPage::stopClicked);
    connect(m_screenshot, &TransportButton::clicked, this, &RecordPage::screenshotClicked);
    connect(m_testLevels, &QPushButton::toggled, this, &RecordPage::testLevelsToggled);
    connect(m_micMute, &QToolButton::toggled, this, [this](bool muted) {
        m_micMute->setIcon(makeIcon(muted ? IconId::MicOff : IconId::Mic, currentPalette().text));
        if (!m_updating)
            m_store.edit(SettingsStore::Audio, [muted](AppSettings& s) { s.micMuted = muted; });
    });
    return f;
}

QWidget* RecordPage::buildDiagnostics(QWidget* parent)
{
    QVBoxLayout* body = nullptr;
    QFrame* f = ui::panel(parent, body, QStringLiteral("Performance"));
    auto* g = new QGridLayout;
    g->setHorizontalSpacing(18);
    g->setVerticalSpacing(8);
    m_dFps = new StatTile(QStringLiteral("Frame rate (encoded)"), f);
    m_dDropped = new StatTile(QStringLiteral("Dropped frames"), f);
    m_dRepeated = new StatTile(QStringLiteral("Unchanged frames"), f);
    m_dCpu = new StatTile(QStringLiteral("CPU (LumaCapture / all)"), f);
    m_dRam = new StatTile(QStringLiteral("RAM"), f);
    m_dGpu = new StatTile(QStringLiteral("GPU (busiest engine)"), f);
    m_dCapLat = new StatTile(QStringLiteral("Capture latency"), f);
    m_dEncLat = new StatTile(QStringLiteral("Encoder latency"), f);
    m_dQueue = new StatTile(QStringLiteral("Encoder queue"), f);
    m_dBitrate = new StatTile(QStringLiteral("Recording bitrate"), f);
    m_dRepeated->setToolTip(QStringLiteral("Frames that repeat an unchanged screen. This is normal for a still screen "
                                           "and is not a drop."));
    StatTile* tiles[] = {m_dFps, m_dDropped, m_dRepeated, m_dCpu, m_dRam, m_dGpu, m_dCapLat, m_dEncLat, m_dQueue, m_dBitrate};
    for (int i = 0; i < 10; ++i)
        g->addWidget(tiles[i], i / 5, i % 5);
    body->addLayout(g);
    m_diagnostics = f;
    f->hide();
    return f;
}

QWidget* RecordPage::buildRecent(QWidget* parent)
{
    QVBoxLayout* body = nullptr;
    QFrame* f = ui::panel(parent, body);
    auto* header = new QHBoxLayout;
    header->addWidget(ui::title(QStringLiteral("Recent Recordings"), f));
    header->addStretch();
    auto* all = new QPushButton(QStringLiteral("View all"), f);
    all->setObjectName("Flat");
    auto* folder = new QPushButton(makeIcon(IconId::Folder, currentPalette().text), QStringLiteral("Open folder"), f);
    header->addWidget(all);
    header->addWidget(folder);
    body->addLayout(header);
    m_recent = new QListWidget(f);
    m_recent->setViewMode(QListView::IconMode);
    m_recent->setFlow(QListView::LeftToRight);
    m_recent->setWrapping(false);
    m_recent->setMovement(QListView::Static);
    m_recent->setUniformItemSizes(true);
    m_recent->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_recent->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recent->setItemDelegate(new RecordingDelegate(m_thumbs, QSize(176, 146), m_recent));
    m_recent->setFixedHeight(166);
    m_recent->setMouseTracking(true);
    m_recent->setContextMenuPolicy(Qt::CustomContextMenu);
    m_recent->setAccessibleName(QStringLiteral("Recent recordings"));
    body->addWidget(m_recent);

    connect(all, &QPushButton::clicked, this, &RecordPage::openLibraryRequested);
    connect(folder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_store.get().outputDir));
    });
    connect(m_recent, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* it) {
        if (!it->data(roles::Missing).toBool())
            QDesktopServices::openUrl(QUrl::fromLocalFile(it->data(roles::Path).toString()));
    });
    connect(m_recent, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* it = m_recent->itemAt(pos);
        if (!it)
            return;
        const QString path = it->data(roles::Path).toString();
        const bool exists = !it->data(roles::Missing).toBool();
        QMenu menu(this);
        menu.addAction(QStringLiteral("Play"), this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); })
            ->setEnabled(exists);
        menu.addAction(QStringLiteral("Show in folder"), this, [path] { showInExplorer(path); })->setEnabled(exists);
        menu.addAction(QStringLiteral("Open in Library"), this, &RecordPage::openLibraryRequested);
        menu.exec(m_recent->viewport()->mapToGlobal(pos));
    });
    return f;
}

// ------------------------------------------------------------------ right

QWidget* RecordPage::buildRight()
{
    auto* f = new QFrame(this);
    f->setObjectName("Panel");
    f->setFixedWidth(372);
    auto* v = new QVBoxLayout(f);
    v->setContentsMargins(10, 4, 4, 10);
    m_tabs = new QTabWidget(f);
    m_tabs->setDocumentMode(true);
    m_videoPanel = new VideoPanel(m_store, m_rec);
    m_audioPanel = new AudioPanel(m_store, m_scanner);
    m_webcamPanel = new WebcamPanel(m_store, m_scanner, m_webcam);
    m_effectsPanel = new EffectsPanel(m_store);
    m_overlaysPanel = new OverlaysPanel(m_store, m_webcam, m_rec);
    m_tabs->addTab(scrolled(m_videoPanel, m_tabs), QStringLiteral("Video"));
    m_tabs->addTab(scrolled(m_audioPanel, m_tabs), QStringLiteral("Audio"));
    m_tabs->addTab(scrolled(m_webcamPanel, m_tabs), QStringLiteral("Webcam"));
    m_tabs->addTab(scrolled(m_effectsPanel, m_tabs), QStringLiteral("Effects"));
    m_tabs->addTab(scrolled(m_overlaysPanel, m_tabs), QStringLiteral("Overlays"));
    m_tabs->setCurrentIndex(std::clamp(m_store.get().contextTab, 0, 4));
    v->addWidget(m_tabs);

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        m_store.edit(SettingsStore::Ui, [i](AppSettings& s) { s.contextTab = i; });
        emit contextTabChanged(i);
    });
    connect(m_audioPanel, &AudioPanel::testLevelsToggled, this, &RecordPage::testLevelsToggled);
    connect(m_overlaysPanel, &OverlaysPanel::openLayoutEditor, this, &RecordPage::openLayoutEditorRequested);
    return f;
}

void RecordPage::showContextTab(Tab tab, bool expandAdvanced)
{
    if (expandAdvanced)
        m_store.edit(SettingsStore::Ui, [](AppSettings& s) { s.advancedVideo = true; });
    m_tabs->setCurrentIndex(static_cast<int>(tab));
}

int RecordPage::currentContextTab() const
{
    return m_tabs->currentIndex();
}

// --------------------------------------------------------------- refresh

void RecordPage::refreshFromSettings(unsigned scope)
{
    ui::Updating guard(m_updating);
    const AppSettings& s = m_store.get();
    if (scope & SettingsStore::Source) {
        if (QAbstractButton* b = m_modeGroup->button(static_cast<int>(s.sourceKind))) {
            const QSignalBlocker block(b);
            b->setChecked(true);
        }
        m_targetStack->setCurrentIndex(static_cast<int>(s.sourceKind));
        m_regionLabel->setText(QStringLiteral("%1 × %2 at (%3, %4) - physical pixels, inside one monitor")
                                   .arg(s.region.width())
                                   .arg(s.region.height())
                                   .arg(s.region.x())
                                   .arg(s.region.y()));
    }
    if (scope & (SettingsStore::Webcam | SettingsStore::Overlays)) {
        const QSignalBlocker block(m_srcWebcam);
        m_srcWebcam->setChecked(s.webcamEnabled);
        m_srcWebcamName->setText(s.webcamName.isEmpty() ? QStringLiteral("No camera selected") : s.webcamName);
    }
    if (scope & SettingsStore::Audio) {
        {
            const QSignalBlocker b1(m_srcMic), b2(m_srcSystem), b3(m_micMute);
            m_srcMic->setChecked(s.microphone);
            m_srcSystem->setChecked(s.systemAudio);
            m_micMute->setChecked(s.micMuted);
        }
        m_micMute->setIcon(makeIcon(s.micMuted ? IconId::MicOff : IconId::Mic, currentPalette().text));
        m_micMute->setEnabled(s.microphone);
        m_srcMicName->setText(s.microphone ? deviceName(m_scanner.captureDevices(), s.micDevice) : QStringLiteral("Off"));
        m_srcSystemName->setText(s.systemAudio ? deviceName(m_scanner.renderDevices(), s.systemDevice)
                                               : QStringLiteral("Off"));
    }
    if (scope & SettingsStore::Output) {
        m_outputDir->setText(QDir::toNativeSeparators(s.outputDir));
        m_outputDir->setToolTip(QDir::toNativeSeparators(s.outputDir));
        const QSignalBlocker b(m_namePattern);
        m_namePattern->setText(s.namePattern);
        ui::selectData(m_container, static_cast<int>(s.container));
    }
    if (scope & (SettingsStore::Video | SettingsStore::Source)) {
        ui::selectData(m_qsResolution, static_cast<int>(s.resolution));
        ui::selectData(m_qsFps, s.fps);
        ui::selectData(m_qsQuality, static_cast<int>(s.quality));
        const int p = m_profile->findText(s.profile);
        const QSignalBlocker b(m_profile);
        m_profile->setCurrentIndex(p >= 0 ? p : m_profile->count() - 1); // "Custom"
    }
    if (scope & SettingsStore::Ui) {
        if (m_tabs->currentIndex() != s.contextTab) {
            const QSignalBlocker b(m_tabs);
            m_tabs->setCurrentIndex(std::clamp(s.contextTab, 0, 4));
        }
    }
    updateSummary();
}

void RecordPage::updateSummary()
{
    const AppSettings& s = m_store.get();
    const QSize src = m_rec.sourceSize(), out = m_rec.outputSize();
    const EncoderChoice enc = resolveEncoder(s);
    const QString size = out.isValid() ? QStringLiteral("%1×%2").arg(out.width()).arg(out.height()) : QStringLiteral("-");
    const QString container = s.container == Container::Mp4 ? QStringLiteral("MP4") : QStringLiteral("MKV");
    m_summary->setText(QStringLiteral("%1 · %2 · %3 FPS · %4 · H.264 (CRF %5)")
                           .arg(m_rec.sourceDescription(), size)
                           .arg(s.fps)
                           .arg(container)
                           .arg(enc.crf));
    QString sourceShort = sourceKindName(s.sourceKind);
    if (s.sourceKind == SourceKind::Display) {
        sourceShort = m_rec.sourceDescription();
        sourceShort.remove(QStringLiteral(" (primary)"));
    }
    m_previewView->setInfo(QStringLiteral("%1  |  %2  |  %3 FPS").arg(sourceShort, size).arg(s.fps));

    // Sources: the screen row.
    QString screen = m_rec.sourceDescription();
    if (src.isValid())
        screen += QStringLiteral(" · %1×%2").arg(src.width()).arg(src.height());
    if (s.sourceKind == SourceKind::Display) {
        for (const MonitorEntry& m : m_scanner.monitors())
            if (m.deviceName == s.displayName && m.refreshHz > 0)
                screen += QStringLiteral(" @ %1 Hz").arg(std::lround(m.refreshHz));
    }
    m_srcDisplayInfo->setText(screen);

    const PerfEstimate pe = estimatePerformance(out, s.fps, enc);
    const Palette& pal = currentPalette();
    const QColor dot = pe.level == PerfLevel::Light || pe.level == PerfLevel::Moderate
                           ? pal.success
                           : (pe.level == PerfLevel::Heavy ? pal.warning : pal.record);
    m_qsPerf->setText(QStringLiteral("<span style='color:%1'>●</span> Performance: <b>%2</b>").arg(dot.name(), pe.label()));
    m_qsPerf->setToolTip(pe.advice().isEmpty() ? enc.description() : pe.advice());

    const QString problem = m_rec.validateSource();
    m_sourceWarning->setText(problem);
    m_sourceWarning->setVisible(!problem.isEmpty() && s.sourceKind != SourceKind::Display);
}

void RecordPage::fillMonitors()
{
    const AppSettings& s = m_store.get();
    const QSignalBlocker block(m_display);
    m_display->clear();
    int select = 0;
    for (const MonitorEntry& m : m_scanner.monitors()) {
        m_display->addItem(makeIcon(IconId::Display, currentPalette().text), m.label(), m.deviceName);
        if (m.deviceName == s.displayName || (s.displayName.isEmpty() && m.primary))
            select = m_display->count() - 1;
    }
    if (m_display->count() == 0) {
        m_display->addItem(QStringLiteral("No display found"));
        return;
    }
    m_display->setCurrentIndex(select);
    if (s.displayName.isEmpty()) {
        const QString name = m_display->itemData(select).toString();
        m_store.edit(SettingsStore::Source, [name](AppSettings& st) { st.displayName = name; });
    } else {
        updateSummary();
    }
}

void RecordPage::fillWindows(const QList<WindowEntry>& windows)
{
    const AppSettings& s = m_store.get();
    auto fill = [&windows](QComboBox* combo, quintptr current, const QString& title) -> int {
        const QSignalBlocker block(combo);
        combo->clear();
        int select = -1;
        for (const WindowEntry& w : windows) {
            const QString text = w.process.isEmpty() ? w.title : QStringLiteral("%1  -  %2").arg(w.title, w.process);
            combo->addItem(text, QVariant::fromValue<quintptr>(w.hwnd));
            combo->setItemData(combo->count() - 1, w.title, Qt::ToolTipRole);
            if (w.hwnd == current || (select < 0 && !current && w.title == title))
                select = combo->count() - 1;
        }
        if (combo->count() == 0) {
            combo->addItem(QStringLiteral("No windows found"));
            return -1;
        }
        combo->setCurrentIndex(select);
        if (select < 0)
            combo->setPlaceholderText(QStringLiteral("Choose a window..."));
        return select;
    };
    const int w = fill(m_window, m_rec.windowTarget(), s.windowTitle);
    if (w >= 0)
        m_rec.setWindowTarget(m_window->itemData(w).value<quintptr>());
    const int g = fill(m_game, m_rec.gameTarget(), s.gameTitle);
    if (g >= 0)
        m_rec.setGameTarget(m_game->itemData(g).value<quintptr>());
    updateSummary();
    m_preview.invalidate();
}

void RecordPage::chooseOutputFolder()
{
    const AppSettings& s = m_store.get();
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Recordings folder"), s.outputDir);
    if (dir.isEmpty())
        return;
    const QString oldShots = s.outputDir + QStringLiteral("/Screenshots");
    m_store.edit(SettingsStore::Output, [dir, oldShots](AppSettings& st) {
        if (QDir::cleanPath(st.screenshotDir) == QDir::cleanPath(oldShots))
            st.screenshotDir = dir + QStringLiteral("/Screenshots");
        st.outputDir = dir;
    });
}

// ------------------------------------------------------------ recording UI

void RecordPage::setRecordingState(session::RecState state)
{
    m_state = state;
    const bool busy = session::isBusy(state);
    if (state == session::RecState::Starting) {
        m_lastBytes = m_lastEncoded = 0;
        m_lastElapsed = 0;
    }
    // Settings that are fixed when the recording starts.
    m_leftContent->setEnabled(!busy);
    m_videoPanel->setBusy(busy);
    m_audioPanel->setBusy(busy);
    m_webcamPanel->setBusy(busy);
    m_micMute->setEnabled(m_store.get().microphone); // mute works live
    m_previewView->setRecording(session::isCapturing(state), state == session::RecState::Paused);
    if (!busy) {
        m_timer->setText(QStringLiteral("00:00:00"));
        m_stateText->setText(state == session::RecState::Error
                                 ? QStringLiteral("The last recording had a problem - see the message above")
                                 : QStringLiteral("Ready to record"));
        resetLevels();
    }
    updateTransport();
}

void RecordPage::updateTransport()
{
    using S = session::RecState;
    const Palette& pal = currentPalette();
    const bool capturing = session::isCapturing(m_state);
    const bool busy = session::isBusy(m_state);
    const bool finishing = m_state == S::Stopping || m_state == S::Finalizing;
    m_record->setEnabled(!busy);
    m_record->setVisual(IconId::Record, busy ? pal.surfaceAlt : pal.record, busy ? pal.record : QColor(Qt::white));
    m_record->setCaption(m_state == S::Starting ? QStringLiteral("Starting...")
                                                : (capturing ? QStringLiteral("Recording") : QStringLiteral("Record")));
    m_pause->setEnabled(capturing);
    m_pause->setVisual(m_state == S::Paused ? IconId::Resume : IconId::Pause, pal.surfaceAlt, pal.text);
    m_pause->setCaption(m_state == S::Paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    m_stop->setEnabled(capturing);
    m_stop->setVisual(IconId::Stop, pal.surfaceAlt, capturing ? pal.record : pal.text);
    m_stop->setCaption(finishing ? QStringLiteral("Saving...") : QStringLiteral("Stop"));
    m_screenshot->setEnabled(m_state != S::Starting && !finishing);
    QString state;
    switch (m_state) {
    case S::Idle: state = QStringLiteral("Ready"); break;
    case S::Starting: state = QStringLiteral("Starting"); break;
    case S::Recording: state = QStringLiteral("Recording"); break;
    case S::Paused: state = QStringLiteral("Paused"); break;
    case S::Stopping:
    case S::Finalizing: state = QStringLiteral("Saving"); break;
    case S::Error: state = QStringLiteral("Error"); break;
    }
    m_timerState->setText(state);
    m_timerState->setObjectName(capturing ? "Warn" : "Dim");
    m_timerState->style()->unpolish(m_timerState);
    m_timerState->style()->polish(m_timerState);
}

void RecordPage::setStatus(const session::SessionStatus& st)
{
    m_timer->setText(formatDuration(st.elapsedSeconds));
    if (!st.sourceStatus.empty())
        m_stateText->setText(QString::fromStdString(st.sourceStatus));
    else if (st.paused)
        m_stateText->setText(QStringLiteral("Paused - the paused time is not part of the video"));
    else
        m_stateText->setText(QStringLiteral("Recording to %1").arg(QFileInfo(m_rec.currentFile()).fileName()));

    if (!m_diagnostics->isVisible())
        return;
    const double dt = st.elapsedSeconds - m_lastElapsed;
    if (dt >= 0.9) {
        const double fps = (st.encodedFrames - m_lastEncoded) / dt;
        const double mbps = static_cast<double>(static_cast<int64_t>(st.bytes) - m_lastBytes) * 8.0 / dt / 1e6;
        m_dFps->setValue(QStringLiteral("%1 FPS").arg(fps, 0, 'f', 1), fps < m_store.get().fps * 0.9 && !st.paused);
        m_dBitrate->setValue(QStringLiteral("%1 Mbit/s").arg(std::max(0.0, mbps), 0, 'f', 1));
        m_lastEncoded = st.encodedFrames;
        m_lastBytes = static_cast<int64_t>(st.bytes);
        m_lastElapsed = st.elapsedSeconds;
    }
    m_dDropped->setValue(QString::number(st.droppedFrames), st.droppedFrames > 0);
    m_dRepeated->setValue(QString::number(st.repeatedFrames));
    m_dCapLat->setValue(QStringLiteral("%1 ms").arg(st.captureLatencyMs, 0, 'f', 1));
    m_dEncLat->setValue(QStringLiteral("%1 ms").arg(st.encodeLatencyMs, 0, 'f', 0));
    m_dQueue->setValue(QStringLiteral("%1 / %2").arg(st.queueDepth).arg(st.queueCapacity),
                       st.queueCapacity > 0 && st.queueDepth + 1 >= st.queueCapacity);
}

void RecordPage::setFinishingText(const QString& text)
{
    m_stateText->setText(text);
}

void RecordPage::setAudioLevels(float system, float mic)
{
    m_systemMeter->setLevel(system);
    m_micMeter->setLevel(mic);
}

void RecordPage::resetLevels()
{
    m_systemMeter->reset();
    m_micMeter->reset();
}

void RecordPage::setTestLevelsActive(bool on)
{
    {
        const QSignalBlocker block(m_testLevels);
        m_testLevels->setChecked(on);
    }
    m_audioPanel->setTestActive(on);
    if (!on)
        resetLevels();
}

void RecordPage::setDiagnosticsVisible(bool visible)
{
    m_diagnostics->setVisible(visible);
}

void RecordPage::setDiagnostics(const DiagnosticsSample& d)
{
    if (!m_diagnostics->isVisible())
        return;
    m_dCpu->setValue(d.processCpu >= 0 ? QStringLiteral("%1% / %2%").arg(d.processCpu, 0, 'f', 0).arg(d.systemCpu, 0, 'f', 0)
                                       : QStringLiteral("-"),
                     d.systemCpu > 90);
    m_dRam->setValue(QStringLiteral("%1 MB").arg(d.ramMb, 0, 'f', 0));
    m_dGpu->setValue(d.gpu >= 0 ? QStringLiteral("%1%").arg(d.gpu, 0, 'f', 0) : QStringLiteral("n/a"));
    if (!session::isCapturing(m_state)) {
        for (StatTile* t : {m_dFps, m_dDropped, m_dRepeated, m_dCapLat, m_dEncLat, m_dQueue, m_dBitrate})
            t->setValue(QStringLiteral("-"));
    }
}

void RecordPage::refreshRecent()
{
    m_recent->clear();
    int n = 0;
    for (const RecordingEntry& e : m_history.entries()) {
        if (n++ >= 12)
            break;
        auto* it = new QListWidgetItem(m_recent);
        fillRecordingItem(it, e);
    }
    if (m_recent->count() == 0) {
        auto* it = new QListWidgetItem(QStringLiteral("No recordings yet"), m_recent);
        it->setData(roles::Meta, QStringLiteral("Press Record - your files appear here"));
        it->setData(roles::Missing, false);
        it->setFlags(Qt::NoItemFlags);
    }
}

} // namespace luma::app
