#include "ui/SettingsPage.h"

#include "AppPaths.h"
#include "AppSettings.h"
#include "HotkeyManager.h"
#include "Icons.h"
#include "SettingsStore.h"
#include "Theme.h"
#include "ui/ContextPanels.h"
#include "ui/UiKit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace luma::app {
namespace {

QCheckBox* check(const QString& text, QWidget* parent, const QString& tip = {})
{
    auto* c = new QCheckBox(text, parent);
    if (!tip.isEmpty())
        c->setToolTip(tip);
    return c;
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

void openFolder(const QString& dir)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

// A hotkey must use a modifier (or be an F-key) so it never interferes with typing.
bool acceptableHotkey(const QKeySequence& seq)
{
    if (seq.isEmpty())
        return true;
    const QKeyCombination combo = seq[0];
    const int key = static_cast<int>(combo.key());
    const bool fKey = key >= Qt::Key_F1 && key <= Qt::Key_F24;
    return fKey || combo.keyboardModifiers().testAnyFlags(Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
}

} // namespace

SettingsPage::SettingsPage(SettingsStore& store, DeviceScanner& scanner, RecordingController& recording,
                           QWidget* aboutPage, QWidget* parent)
    : QWidget(parent), m_store(store), m_scanner(scanner), m_rec(recording)
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 12);
    root->setSpacing(14);
    m_nav = new QListWidget(this);
    m_nav->setObjectName("Nav");
    m_nav->setFixedWidth(200);
    m_nav->setAccessibleName(QStringLiteral("Settings sections"));
    for (const char* name : {"General", "Recording", "Video", "Audio", "Webcam", "Hotkeys", "Output", "Appearance",
                             "Advanced", "About & feedback"})
        m_nav->addItem(QString::fromLatin1(name));
    m_stack = new QStackedWidget(this);
    root->addWidget(m_nav);
    root->addWidget(m_stack, 1);

    m_video = new VideoPanel(store, recording);
    m_audio = new AudioPanel(store, scanner);
    m_stack->addWidget(scrollPage(QStringLiteral("General"), buildGeneral()));
    m_stack->addWidget(scrollPage(QStringLiteral("Recording"), buildRecording()));
    m_stack->addWidget(scrollPage(QStringLiteral("Video"), m_video));
    m_stack->addWidget(scrollPage(QStringLiteral("Audio"), m_audio));
    m_stack->addWidget(scrollPage(QStringLiteral("Webcam"), buildWebcam()));
    m_stack->addWidget(scrollPage(QStringLiteral("Hotkeys"), buildHotkeys()));
    m_stack->addWidget(scrollPage(QStringLiteral("Output"), buildOutput()));
    m_stack->addWidget(scrollPage(QStringLiteral("Appearance"), buildAppearance()));
    m_stack->addWidget(scrollPage(QStringLiteral("Advanced"), buildAdvanced()));
    m_stack->addWidget(scrollPage(QStringLiteral("About & feedback"), aboutPage));
    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_audio, &AudioPanel::testLevelsToggled, this, &SettingsPage::testLevelsToggled);
    m_nav->setCurrentRow(0);

    connect(&m_store, &SettingsStore::changed, this, [this](unsigned scope) {
        if (scope & (SettingsStore::General | SettingsStore::Output | SettingsStore::Hotkeys | SettingsStore::Appearance |
                     SettingsStore::Video | SettingsStore::Source))
            refresh();
    });
    refresh();
}

QWidget* SettingsPage::scrollPage(const QString& title, QWidget* content)
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);
    auto* t = new QLabel(title, page);
    t->setObjectName("PageTitle");
    v->addWidget(t);
    auto* sa = new QScrollArea(page);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    auto* holder = new QWidget;
    auto* hv = new QVBoxLayout(holder);
    hv->setContentsMargins(0, 0, 12, 0);
    content->setMaximumWidth(760);
    hv->addWidget(content);
    hv->addStretch();
    sa->setWidget(holder);
    v->addWidget(sa, 1);
    return page;
}

void SettingsPage::showSection(Section s)
{
    m_nav->setCurrentRow(static_cast<int>(s));
}

QWidget* SettingsPage::buildGeneral()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* body = nullptr;
    v->addWidget(ui::panel(w, body, QStringLiteral("While recording")));
    m_minimizeOnRecord = check(QStringLiteral("Minimize LumaCapture when recording starts"), w);
    m_countdown = new QSpinBox;
    m_countdown->setRange(0, 10);
    m_countdown->setSuffix(QStringLiteral(" s"));
    m_countdown->setSpecialValueText(QStringLiteral("No countdown"));
    m_hud = check(QStringLiteral("Show a small recording control bar (HUD)"), w,
                  QStringLiteral("Off by default. The bar is kept out of the recording (excluded from capture)."));
    m_exclude = check(QStringLiteral("Keep LumaCapture's own windows out of recordings and screenshots"), w,
                      QStringLiteral("Uses Windows' capture exclusion (Windows 10 2004 or later). If Windows refuses, "
                                     "LumaCapture minimises itself while recording the screen."));
    body->addWidget(m_minimizeOnRecord);
    body->addWidget(ui::row(QStringLiteral("Countdown"), m_countdown, w));
    body->addWidget(m_hud);
    body->addWidget(m_exclude);

    QVBoxLayout* body2 = nullptr;
    v->addWidget(ui::panel(w, body2, QStringLiteral("Startup and window")));
    m_tray = check(QStringLiteral("Minimizing hides LumaCapture to the notification area"), w);
    m_recovery = check(QStringLiteral("Look for interrupted recordings at startup"), w,
                       QStringLiteral("After a crash or power loss, LumaCapture offers to repair the file"));
    auto* scan = new QPushButton(QStringLiteral("Look for interrupted recordings now"), w);
    body2->addWidget(m_tray);
    body2->addWidget(m_recovery);
    body2->addWidget(scan, 0, Qt::AlignLeft);

    auto bind = [this](QCheckBox* box, auto setter) {
        connect(box, &QCheckBox::toggled, this, [this, setter](bool on) {
            if (!m_updating)
                m_store.edit(SettingsStore::General, [&setter, on](AppSettings& s) { setter(s, on); });
        });
    };
    bind(m_minimizeOnRecord, [](AppSettings& s, bool on) { s.minimizeOnRecord = on; });
    bind(m_hud, [](AppSettings& s, bool on) { s.recordingHud = on; });
    bind(m_exclude, [](AppSettings& s, bool on) { s.excludeOwnWindows = on; });
    bind(m_tray, [](AppSettings& s, bool on) { s.minimizeToTray = on; });
    bind(m_recovery, [](AppSettings& s, bool on) { s.checkRecoveryOnStart = on; });
    connect(m_countdown, &QSpinBox::valueChanged, this, [this](int v) {
        if (!m_updating)
            m_store.edit(SettingsStore::General, [v](AppSettings& s) { s.countdownSeconds = v; });
    });
    connect(scan, &QPushButton::clicked, this, &SettingsPage::recoveryScanRequested);
    return w;
}

QWidget* SettingsPage::buildRecording()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* body = nullptr;
    v->addWidget(ui::panel(w, body, QStringLiteral("File")));
    m_container = new QComboBox;
    m_container->addItem(QStringLiteral("MKV (recommended)"), 0);
    m_container->addItem(QStringLiteral("MP4 (converted from MKV after recording)"), 1);
    m_keepMkv = check(QStringLiteral("Keep the MKV after converting to MP4"), w);
    m_lowSpace = new QSpinBox;
    m_lowSpace->setRange(0, 100000);
    m_lowSpace->setSingleStep(500);
    m_lowSpace->setSuffix(QStringLiteral(" MB"));
    body->addWidget(ui::row(QStringLiteral("Container"), m_container, w));
    body->addWidget(m_keepMkv);
    body->addWidget(ui::row(QStringLiteral("Warn below"), m_lowSpace, w,
                            QStringLiteral("Asks before recording when the recording drive has less free space. Below "
                                           "300 MB a running recording is stopped and saved.")));
    body->addWidget(ui::hint(QStringLiteral("Recordings are written crash-safely: at most about one second is lost if "
                                            "the PC crashes or loses power."),
                             w));

    QVBoxLayout* body2 = nullptr;
    v->addWidget(ui::panel(w, body2, QStringLiteral("Live preview and warnings")));
    m_preview = check(QStringLiteral("Live preview on the Record page"), w,
                      QStringLiteral("A small, low-frame-rate view of exactly what will be recorded. Rendered on the "
                                     "graphics card; paused while the window is minimised."));
    m_previewFps = new QSpinBox;
    m_previewFps->setRange(2, 15);
    m_previewFps->setSuffix(QStringLiteral(" FPS"));
    m_previewRec = check(QStringLiteral("Keep the preview running while recording"), w,
                         QStringLiteral("Off saves a little CPU during recordings"));
    m_behind = check(QStringLiteral("Warn when the recording is falling behind (dropping frames)"), w);
    body2->addWidget(m_preview);
    body2->addWidget(ui::row(QStringLiteral("Preview rate"), m_previewFps, w));
    body2->addWidget(m_previewRec);
    body2->addWidget(m_behind);

    QVBoxLayout* body3 = nullptr;
    v->addWidget(ui::panel(w, body3, QStringLiteral("Crop (Display and Window capture)")));
    const char* sides[] = {"Left", "Top", "Right", "Bottom"};
    for (int i = 0; i < 4; ++i) {
        m_crop[i] = new QSpinBox;
        m_crop[i]->setRange(0, 4000);
        m_crop[i]->setSuffix(QStringLiteral(" px"));
        body3->addWidget(ui::row(QString::fromLatin1(sides[i]), m_crop[i], w));
        connect(m_crop[i], &QSpinBox::valueChanged, this, [this, i](int value) {
            if (m_updating)
                return;
            m_store.edit(SettingsStore::Source, [i, value](AppSettings& s) {
                int* fields[] = {&s.cropLeft, &s.cropTop, &s.cropRight, &s.cropBottom};
                *fields[i] = value;
            });
        });
    }
    body3->addWidget(ui::hint(QStringLiteral("Cuts pixels off the edges of the captured display or window. Region "
                                             "capture uses its own rectangle instead."),
                              w));

    auto edit = [this](unsigned scope, auto change) {
        if (!m_updating)
            m_store.edit(scope, change);
    };
    connect(m_container, &QComboBox::activated, this, [this, edit](int i) {
        const int c = m_container->itemData(i).toInt();
        edit(SettingsStore::Output, [c](AppSettings& s) { s.container = static_cast<Container>(c); });
    });
    connect(m_keepMkv, &QCheckBox::toggled, this,
            [edit](bool on) { edit(SettingsStore::Output, [on](AppSettings& s) { s.keepMkvAfterMp4 = on; }); });
    connect(m_lowSpace, &QSpinBox::valueChanged, this,
            [edit](int v) { edit(SettingsStore::General, [v](AppSettings& s) { s.lowSpaceWarnMB = v; }); });
    connect(m_preview, &QCheckBox::toggled, this,
            [edit](bool on) { edit(SettingsStore::General, [on](AppSettings& s) { s.livePreview = on; }); });
    connect(m_previewFps, &QSpinBox::valueChanged, this,
            [edit](int v) { edit(SettingsStore::General, [v](AppSettings& s) { s.previewFps = v; }); });
    connect(m_previewRec, &QCheckBox::toggled, this,
            [edit](bool on) { edit(SettingsStore::General, [on](AppSettings& s) { s.previewWhileRecording = on; }); });
    connect(m_behind, &QCheckBox::toggled, this,
            [edit](bool on) { edit(SettingsStore::General, [on](AppSettings& s) { s.warnFallingBehind = on; }); });
    return w;
}

QWidget* SettingsPage::buildWebcam()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* body = nullptr;
    v->addWidget(ui::panel(w, body, QStringLiteral("Webcam")));
    body->addWidget(ui::hint(QStringLiteral("Camera selection, Auto adjust, picture settings, anti-flicker, low-light "
                                            "behaviour and the green screen are on the Record page (Webcam tab), next "
                                            "to the live camera image. Placement, size and shape are under Overlays."),
                             w));
    auto* open = new QPushButton(makeIcon(IconId::Camera, currentPalette().text), QStringLiteral("Open webcam settings"), w);
    body->addWidget(open, 0, Qt::AlignLeft);
    connect(open, &QPushButton::clicked, this, &SettingsPage::openWebcamSettings);
    return w;
}

QWidget* SettingsPage::buildHotkeys()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* body = nullptr;
    v->addWidget(ui::panel(w, body, QStringLiteral("Global hotkeys")));
    body->addWidget(ui::hint(QStringLiteral("Hotkeys work while other programs have focus. Use combinations with Ctrl, "
                                            "Alt or Win (or an F-key) so they never interfere with typing. Combinations "
                                            "already used by another program are reported below."),
                             w));
    for (int i = 0; i < static_cast<int>(HotkeyAction::Count); ++i) {
        auto* edit = new QKeySequenceEdit;
        edit->setMaximumSequenceLength(1);
        edit->setClearButtonEnabled(true);
        m_keys[i] = edit;
        body->addWidget(ui::row(AppSettings::hotkeyName(static_cast<HotkeyAction>(i)), edit, w));
        connect(edit, &QKeySequenceEdit::editingFinished, this, [this, i] {
            const QKeySequence seq = m_keys[i]->keySequence();
            if (!acceptableHotkey(seq)) {
                emit notify(2, QStringLiteral("Use a combination with Ctrl, Alt or Win (or an F-key) so the hotkey does "
                                              "not interfere with typing."));
                refresh();
                return;
            }
            unsigned mods = 0, vk = 0;
            if (!seq.isEmpty() && !HotkeyManager::toNative(seq, mods, vk)) {
                emit notify(2, QStringLiteral("This key cannot be used as a global hotkey."));
                refresh();
                return;
            }
            m_store.edit(SettingsStore::Hotkeys, [i, seq](AppSettings& s) { s.hotkeys[i] = seq; });
        });
    }
    m_hotkeyStatus = new QLabel(w);
    m_hotkeyStatus->setWordWrap(true);
    body->addWidget(m_hotkeyStatus);
    auto* defaults = new QPushButton(QStringLiteral("Restore default hotkeys"), w);
    body->addWidget(defaults, 0, Qt::AlignLeft);
    connect(defaults, &QPushButton::clicked, this, [this] {
        const AppSettings fresh = defaultSettings();
        m_store.edit(SettingsStore::Hotkeys, [&fresh](AppSettings& s) {
            for (int i = 0; i < static_cast<int>(HotkeyAction::Count); ++i)
                s.hotkeys[i] = fresh.hotkeys[i];
        });
    });
    return w;
}

QWidget* SettingsPage::buildOutput()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* body = nullptr;
    v->addWidget(ui::panel(w, body, QStringLiteral("Folders")));
    auto folderRow = [w, this](QLineEdit*& edit, const QString& label, bool screenshots) {
        auto* row = new QWidget(w);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        edit = new QLineEdit(row);
        edit->setReadOnly(true);
        auto* browse = new QPushButton(QStringLiteral("Change..."), row);
        auto* open = new QPushButton(QStringLiteral("Open"), row);
        h->addWidget(edit, 1);
        h->addWidget(browse);
        h->addWidget(open);
        QLineEdit* e = edit;
        connect(browse, &QPushButton::clicked, this, [this, e, screenshots, label] {
            const QString dir = QFileDialog::getExistingDirectory(this, label, QDir::fromNativeSeparators(e->text()));
            if (dir.isEmpty())
                return;
            m_store.edit(SettingsStore::Output, [dir, screenshots](AppSettings& s) {
                if (screenshots)
                    s.screenshotDir = dir;
                else
                    s.outputDir = dir;
            });
        });
        connect(open, &QPushButton::clicked, this, [e] { openFolder(QDir::fromNativeSeparators(e->text())); });
        return ui::row(label, row, w);
    };
    body->addWidget(folderRow(m_outputDir, QStringLiteral("Recordings"), false));
    body->addWidget(folderRow(m_shotDir, QStringLiteral("Screenshots"), true));

    QVBoxLayout* body2 = nullptr;
    v->addWidget(ui::panel(w, body2, QStringLiteral("Names")));
    m_pattern = new QLineEdit;
    body2->addWidget(ui::row(QStringLiteral("File name"), m_pattern, w,
                             QStringLiteral("Placeholders: {date} {time} {res} {fps} {source}")));
    m_shotCursor = check(QStringLiteral("Include the mouse cursor in screenshots"), w);
    body2->addWidget(m_shotCursor);
    connect(m_pattern, &QLineEdit::editingFinished, this, [this] {
        const QString p = m_pattern->text().trimmed();
        m_store.edit(SettingsStore::Output, [p](AppSettings& s) {
            s.namePattern = p.isEmpty() ? QStringLiteral("LumaCapture_{date}_{time}") : p;
        });
    });
    connect(m_shotCursor, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating)
            m_store.edit(SettingsStore::Output, [on](AppSettings& s) { s.screenshotCursor = on; });
    });
    return w;
}

QWidget* SettingsPage::buildAppearance()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* body = nullptr;
    v->addWidget(ui::panel(w, body, QStringLiteral("Appearance")));
    m_theme = new QComboBox;
    m_theme->addItem(QStringLiteral("Dark"), static_cast<int>(Theme::Dark));
    m_theme->addItem(QStringLiteral("Light"), static_cast<int>(Theme::Light));
    body->addWidget(ui::row(QStringLiteral("Theme"), m_theme, w));
    body->addWidget(ui::hint(QStringLiteral("Text and controls follow the Windows display scaling setting. The theme "
                                            "uses plain colours only - no animated or blurred effects - so the window "
                                            "costs almost nothing to draw while recording."),
                             w));
    connect(m_theme, &QComboBox::activated, this, [this](int i) {
        const auto t = static_cast<Theme>(m_theme->itemData(i).toInt());
        m_store.edit(SettingsStore::Appearance, [t](AppSettings& s) { s.theme = t; });
    });
    return w;
}

QWidget* SettingsPage::buildAdvanced()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* body = nullptr;
    v->addWidget(ui::panel(w, body, QStringLiteral("Diagnostics")));
    m_diagnostics = check(QStringLiteral("Show the performance panel on the Record page"), w,
                          QStringLiteral("Frame rate, dropped frames, CPU, RAM, GPU, latency, queue and bitrate. Off by "
                                         "default; measured about once per second."));
    m_gpuConvert = check(QStringLiteral("GPU colour conversion (recommended)"), w,
                         QStringLiteral("Off = convert on the CPU (fallback for driver problems; costs CPU and disables "
                                        "the live preview)."));
    body->addWidget(m_diagnostics);
    body->addWidget(m_gpuConvert);

    QVBoxLayout* body2 = nullptr;
    v->addWidget(ui::panel(w, body2, QStringLiteral("Files")));
    body2->addWidget(ui::hint(QStringLiteral("LumaCapture keeps its settings, library, logs and crash reports in: %1. "
                                             "Nothing is ever uploaded.")
                                  .arg(QDir::toNativeSeparators(AppPaths::dataDir())),
                              w));
    auto* buttons = new QHBoxLayout;
    auto* data = new QPushButton(QStringLiteral("Open data folder"), w);
    auto* logs = new QPushButton(QStringLiteral("Open logs"), w);
    auto* dumps = new QPushButton(QStringLiteral("Open crash reports"), w);
    buttons->addWidget(data);
    buttons->addWidget(logs);
    buttons->addWidget(dumps);
    buttons->addStretch();
    body2->addLayout(buttons);

    QVBoxLayout* body3 = nullptr;
    v->addWidget(ui::panel(w, body3, QStringLiteral("Reset")));
    auto* reset = new QPushButton(QStringLiteral("Reset all settings..."), w);
    reset->setObjectName("Danger");
    body3->addWidget(reset, 0, Qt::AlignLeft);

    connect(m_diagnostics, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating)
            m_store.edit(SettingsStore::General, [on](AppSettings& s) { s.diagnostics = on; });
    });
    connect(m_gpuConvert, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating)
            m_store.edit(SettingsStore::Video, [on](AppSettings& s) { s.cpuConvert = !on; });
    });
    connect(data, &QPushButton::clicked, this, [] { openFolder(AppPaths::dataDir()); });
    connect(logs, &QPushButton::clicked, this, [] { openFolder(AppPaths::logDir()); });
    connect(dumps, &QPushButton::clicked, this, [] { openFolder(AppPaths::crashDumpDir()); });
    connect(reset, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("Reset all settings"),
                                  QStringLiteral("Reset every setting to its default? Your recordings and the library "
                                                 "are not touched; the recordings folder is kept.")) != QMessageBox::Yes)
            return;
        AppSettings fresh = defaultSettings();
        fresh.outputDir = m_store.get().outputDir;
        fresh.screenshotDir = m_store.get().screenshotDir;
        m_store.replace(fresh);
        emit notify(1, QStringLiteral("All settings were reset."));
    });
    return w;
}

void SettingsPage::refresh()
{
    ui::Updating guard(m_updating);
    const AppSettings& s = m_store.get();
    setChecked(m_minimizeOnRecord, s.minimizeOnRecord);
    setSpin(m_countdown, s.countdownSeconds);
    setChecked(m_hud, s.recordingHud);
    setChecked(m_exclude, s.excludeOwnWindows);
    setChecked(m_tray, s.minimizeToTray);
    setChecked(m_recovery, s.checkRecoveryOnStart);
    ui::selectData(m_container, static_cast<int>(s.container));
    setChecked(m_keepMkv, s.keepMkvAfterMp4);
    m_keepMkv->setEnabled(s.container == Container::Mp4);
    setSpin(m_lowSpace, s.lowSpaceWarnMB);
    setChecked(m_preview, s.livePreview);
    setSpin(m_previewFps, s.previewFps);
    setChecked(m_previewRec, s.previewWhileRecording);
    setChecked(m_behind, s.warnFallingBehind);
    const int crops[] = {s.cropLeft, s.cropTop, s.cropRight, s.cropBottom};
    for (int i = 0; i < 4; ++i)
        setSpin(m_crop[i], crops[i]);
    for (int i = 0; i < static_cast<int>(HotkeyAction::Count); ++i) {
        const QSignalBlocker block(m_keys[i]);
        m_keys[i]->setKeySequence(s.hotkeys[i]);
    }
    m_outputDir->setText(QDir::toNativeSeparators(s.outputDir));
    m_shotDir->setText(QDir::toNativeSeparators(s.screenshotDir));
    {
        const QSignalBlocker block(m_pattern);
        m_pattern->setText(s.namePattern);
    }
    setChecked(m_shotCursor, s.screenshotCursor);
    ui::selectData(m_theme, static_cast<int>(s.theme));
    setChecked(m_diagnostics, s.diagnostics);
    setChecked(m_gpuConvert, !s.cpuConvert);
}

void SettingsPage::setBusy(bool busy)
{
    m_video->setBusy(busy);
    m_audio->setBusy(busy);
    for (QWidget* w : std::initializer_list<QWidget*>{m_container, m_keepMkv, m_outputDir, m_pattern, m_gpuConvert,
                                                      m_crop[0], m_crop[1], m_crop[2], m_crop[3]})
        w->setEnabled(!busy);
}

void SettingsPage::setHotkeyConflicts(const QStringList& conflicts)
{
    if (conflicts.isEmpty()) {
        m_hotkeyStatus->setObjectName("Ok");
        m_hotkeyStatus->setText(QStringLiteral("All hotkeys are registered."));
    } else {
        m_hotkeyStatus->setObjectName("Warn");
        m_hotkeyStatus->setText(QStringLiteral("Not available: %1").arg(conflicts.join(QStringLiteral("; "))));
    }
    m_hotkeyStatus->style()->unpolish(m_hotkeyStatus); // re-polish for the new object name
    m_hotkeyStatus->style()->polish(m_hotkeyStatus);
}

void SettingsPage::setTestLevelsActive(bool on)
{
    m_audio->setTestActive(on);
}

} // namespace luma::app
