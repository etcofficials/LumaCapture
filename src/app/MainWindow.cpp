#include "MainWindow.h"

#include "AppPaths.h"
#include "AudioMonitor.h"
#include "CrashHandler.h"
#include "HotkeyManager.h"
#include "Icons.h"
#include "LayoutDialog.h"
#include "MediaImporter.h"
#include "PreviewController.h"
#include "SettingsStore.h"
#include "Theme.h"
#include "ThumbnailCache.h"
#include "WebcamController.h"
#include "WinUtil.h"
#include "ui/AboutPage.h"
#include "ui/LibraryPage.h"
#include "ui/RecordPage.h"
#include "ui/SettingsPage.h"
#include "ui/WebcamPanel.h"
#include "util/GpuMetrics.h"
#include "util/Log.h"
#include "util/ProcessMetrics.h"
#include "widgets/Widgets.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <windows.h>

#include <mutex>

namespace luma::app {

// Process/GPU counters for the diagnostics panel; sampled on a worker thread.
struct DiagnosticsState {
    std::mutex mutex;
    std::unique_ptr<ProcessMetrics> process;
    std::unique_ptr<GpuMetrics> gpu;
};

namespace {

QStringList droppedVideos(const QMimeData* mime)
{
    QStringList files;
    if (!mime || !mime->hasUrls())
        return files;
    for (const QUrl& u : mime->urls())
        if (u.isLocalFile() && MediaImporter::hasSupportedExtension(u.toLocalFile()))
            files << u.toLocalFile();
    return files;
}

} // namespace

unsigned MainWindow::activateMessageId()
{
    static const UINT id = RegisterWindowMessageW(L"LumaCapture.ShowWindow");
    return id;
}

MainWindow::MainWindow(const AutomationOptions& automation, QWidget* parent)
    : QMainWindow(parent), m_automation(automation), m_history(AppPaths::historyFile())
{
    m_store = new SettingsStore(AppPaths::settingsFile(), this);
    const bool automationRun = automation.selfTestSeconds > 0 || !automation.snapshotDir.isEmpty();
    if (automationRun) {
        m_store->setPersistent(false); // automation never changes the user's settings
        AppSettings& s = m_store->ref();
        if (!automation.outputDir.isEmpty())
            s.outputDir = automation.outputDir;
        s.countdownSeconds = 0;
        s.minimizeOnRecord = false;
    }
    applyTheme(*qApp, m_store->get().theme);
    m_history.load();

    setWindowTitle(QStringLiteral("LumaCapture"));
    setWindowIcon(makeIcon(IconId::App, Qt::white));
    resize(1440, 900);
    setMinimumSize(1200, 720);
    setAcceptDrops(true);

    m_scanner = new DeviceScanner(this);
    m_webcam = new WebcamController(this);
    m_controller = new RecordingController(m_store->ref(), *m_webcam, *m_scanner, m_history, this);
    m_preview = new PreviewController(*m_controller, this);
    m_hotkeys = new HotkeyManager(this);
    m_audioTest = new AudioMonitor(this);
    m_thumbs = new ThumbnailCache(this);
    m_importer = new MediaImporter(this);
    m_diag = std::make_shared<DiagnosticsState>();

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildTopBar());
    m_banner = new Banner(central);
    auto* bannerWrap = new QWidget(central);
    auto* bw = new QVBoxLayout(bannerWrap);
    bw->setContentsMargins(12, 8, 12, 0);
    bw->addWidget(m_banner);
    root->addWidget(bannerWrap);
    m_pages = new QStackedWidget(central);
    m_recordPage = new RecordPage(*m_store, *m_scanner, *m_webcam, *m_controller, *m_preview, m_history, *m_thumbs, m_pages);
    m_libraryPage = new LibraryPage(m_history, *m_thumbs, *m_importer, *m_store, m_pages);
    auto* aboutInSettings = new AboutPage;
    m_settingsPage = new SettingsPage(*m_store, *m_scanner, *m_controller, aboutInSettings, m_pages);
    auto* aboutWrap = new QWidget(m_pages);
    auto* aw = new QVBoxLayout(aboutWrap);
    aw->setContentsMargins(16, 14, 16, 12);
    m_aboutPage = new AboutPage(aboutWrap);
    m_aboutPage->setMaximumWidth(820);
    aw->addWidget(m_aboutPage);
    m_pages->addWidget(m_recordPage);
    m_pages->addWidget(m_libraryPage);
    m_pages->addWidget(m_settingsPage);
    m_pages->addWidget(aboutWrap);
    root->addWidget(m_pages, 1);
    setCentralWidget(central);

    m_footerPath = new QLabel(this);
    m_footerFree = new QLabel(this);
    statusBar()->addWidget(m_footerPath, 1);
    statusBar()->addPermanentWidget(m_footerFree);
    statusBar()->setSizeGripEnabled(true);

    m_hud = new RecordingBar(nullptr);
    connect(m_hud, &RecordingBar::pauseClicked, m_controller, &RecordingController::togglePause);
    connect(m_hud, &RecordingBar::stopClicked, m_controller, &RecordingController::stop);
    connect(m_hud, &RecordingBar::micClicked, this, [this] {
        m_store->edit(SettingsStore::Audio, [](AppSettings& s) { s.micMuted = !s.micMuted; });
    });
    buildTray();

    // Timers ---------------------------------------------------------------------------
    m_liveTimer.setSingleShot(true);
    m_liveTimer.setInterval(150);
    connect(&m_liveTimer, &QTimer::timeout, m_controller, &RecordingController::updateLive);
    m_meterTimer.setInterval(40);
    connect(&m_meterTimer, &QTimer::timeout, this, [this] {
        float s = 0, m = 0;
        m_controller->takeAudioPeaks(s, m);
        m_recordPage->setAudioLevels(s, m);
    });
    m_heartbeat.setInterval(500);
    connect(&m_heartbeat, &QTimer::timeout, this, [] { crash::heartbeat(); });
    m_heartbeat.start();
    m_finishTicker.setInterval(500);
    connect(&m_finishTicker, &QTimer::timeout, this, [this] {
        const double s = m_controller->finishingSeconds();
        QString text = m_controller->state() == session::RecState::Finalizing
                           ? QStringLiteral("Finalising the file (%1 s)...").arg(s, 0, 'f', 0)
                           : QStringLiteral("Stopping and saving (%1 s)...").arg(s, 0, 'f', 0);
        if (s > 20)
            text += QStringLiteral(" Large files, MP4 conversion or a slow disk can take a while.");
        m_recordPage->setFinishingText(text);
    });
    m_diagTimer.setInterval(1000);
    connect(&m_diagTimer, &QTimer::timeout, this, &MainWindow::sampleDiagnostics);

    // Controllers -----------------------------------------------------------------------
    connect(m_store, &SettingsStore::changed, this, &MainWindow::onSettingsChanged);
    connect(m_controller, &RecordingController::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_controller, &RecordingController::statusTick, this, &MainWindow::onStatusTick);
    connect(m_controller, &RecordingController::errorOccurred, this, &MainWindow::showError);
    connect(m_controller, &RecordingController::info, this, [this](const QString& m) { notify(0, m, 5000); });
    connect(m_controller, &RecordingController::fallingBehind, this, [this](double pct) {
        notify(2,
               QStringLiteral("Recording is falling behind (%1% of frames dropped). Consider lowering resolution, FPS, "
                              "or quality.")
                   .arg(pct, 0, 'f', 1),
               15000);
    });
    connect(m_controller, &RecordingController::historyChanged, this, [this] {
        m_recordPage->refreshRecent();
        m_libraryPage->refresh();
    });
    connect(m_controller, &RecordingController::recordingSaved, this, [this](const QString& f, bool withErrors) {
        m_savedHint = f;
        notify(withErrors ? 2 : 1,
               QStringLiteral("Saved %1").arg(QDir::toNativeSeparators(f)) +
                   (withErrors ? QStringLiteral(" (with problems - see the message)") : QString()),
               12000);
        if (m_tray && m_tray->isVisible() && !isVisible())
            m_tray->showMessage(QStringLiteral("LumaCapture"), QStringLiteral("Recording saved"));
        updateFooter();
    });
    connect(m_controller, &RecordingController::screenshotSaved, this, [this](const QString& f) {
        m_savedHint = f;
        notify(1, QStringLiteral("Screenshot saved: %1").arg(QDir::toNativeSeparators(f)), 8000);
    });
    connect(m_controller, &RecordingController::recoveryFound, this, [this](const QList<RecoveryCandidate>& items) {
        if (m_automation.selfTestSeconds > 0 || !m_automation.snapshotDir.isEmpty())
            return; // never prompt during automation
        QStringList names;
        for (const RecoveryCandidate& c : items)
            names << QStringLiteral("%1 (%2)").arg(QFileInfo(c.media).fileName(), formatBytes(static_cast<uint64_t>(c.size)));
        QMessageBox box(QMessageBox::Question, QStringLiteral("Interrupted recordings found"),
                        QStringLiteral("These recordings were not finished properly (for example after a crash or "
                                       "power loss):\n\n%1\n\nLumaCapture can write a repaired copy "
                                       "(\"-recovered.mkv\") next to each file. The originals are kept.")
                            .arg(names.join('\n')),
                        QMessageBox::NoButton, this);
        auto* recover = box.addButton(QStringLiteral("Recover"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Not now"), QMessageBox::RejectRole);
        exclude(&box);
        box.exec();
        if (box.clickedButton() == recover)
            m_controller->recover(items);
    });
    connect(m_scanner, &DeviceScanner::audioReady, this, [this] { m_recordPage->update(); });
    connect(m_scanner, &DeviceScanner::monitorsReady, m_preview, &PreviewController::invalidate);
    connect(m_audioTest, &AudioMonitor::levels, this, [this](float s, float m) { m_recordPage->setAudioLevels(s, m); });
    connect(m_banner, &Banner::actionClicked, this, [this] {
        if (!m_savedHint.isEmpty())
            showInExplorer(m_savedHint);
    });
    connect(m_hotkeys, &HotkeyManager::triggered, this, &MainWindow::onHotkey);
    connect(qApp, &QGuiApplication::screenAdded, m_scanner, &DeviceScanner::scanMonitors);
    connect(qApp, &QGuiApplication::screenRemoved, m_scanner, &DeviceScanner::scanMonitors);

    // Pages -----------------------------------------------------------------------------
    connect(m_recordPage, &RecordPage::recordClicked, this, &MainWindow::onRecordClicked);
    connect(m_recordPage, &RecordPage::pauseClicked, m_controller, &RecordingController::togglePause);
    connect(m_recordPage, &RecordPage::stopClicked, m_controller, &RecordingController::stop);
    connect(m_recordPage, &RecordPage::screenshotClicked, m_controller, &RecordingController::takeScreenshot);
    connect(m_recordPage, &RecordPage::selectRegionRequested, this, &MainWindow::selectRegion);
    connect(m_recordPage, &RecordPage::openLibraryRequested, this, [this] { showPage(LibraryPg); });
    connect(m_recordPage, &RecordPage::openLayoutEditorRequested, this, &MainWindow::openLayoutEditor);
    connect(m_recordPage, &RecordPage::testLevelsToggled, this, &MainWindow::setTestLevels);
    connect(m_recordPage, &RecordPage::refreshDevicesRequested, this, [this] {
        m_scanner->scanMonitors();
        m_scanner->scanCameras();
        m_scanner->scanAudio();
        m_scanner->scanWindows();
        notify(0, QStringLiteral("Looking for displays, cameras and audio devices..."), 3000);
    });
    connect(m_recordPage, &RecordPage::contextTabChanged, this, [this] { updatePreviewVisibility(); });
    connect(m_recordPage, &RecordPage::notify, this, [this](int k, const QString& t) { notify(k, t); });
    connect(m_recordPage->previewView(), &PreviewView::resized, this, [this] { updatePreviewVisibility(); });
    connect(m_libraryPage, &LibraryPage::notify, this, [this](int k, const QString& t) { notify(k, t, k >= 2 ? 0 : 6000); });
    connect(m_libraryPage, &LibraryPage::historyEdited, m_recordPage, &RecordPage::refreshRecent);
    connect(m_settingsPage, &SettingsPage::openWebcamSettings, this, [this] {
        showPage(RecordPg);
        m_recordPage->showContextTab(RecordPage::WebcamTab);
    });
    connect(m_settingsPage, &SettingsPage::recoveryScanRequested, m_controller, &RecordingController::scanForRecovery);
    connect(m_settingsPage, &SettingsPage::testLevelsToggled, this, &MainWindow::setTestLevels);
    connect(m_settingsPage, &SettingsPage::notify, this, [this](int k, const QString& t) { notify(k, t); });

    // Start-up ------------------------------------------------------------------------------
    const AppSettings& s = m_store->get();
    m_excludeOk = exclude(this); // before the window is first shown
    setDarkTitleBar(this, s.theme == Theme::Dark);
    m_scanner->scanMonitors();
    m_scanner->scanCameras();
    m_scanner->scanAudio();
    if (s.sourceKind == SourceKind::Window || s.sourceKind == SourceKind::Game)
        m_scanner->scanWindows();
    applyWebcam();
    m_preview->setEnabled(s.livePreview && !s.cpuConvert);
    m_recordPage->setDiagnosticsVisible(s.diagnostics);
    if (s.diagnostics)
        m_diagTimer.start();
    updateFooter();
    if (!automationRun && s.checkRecoveryOnStart)
        m_controller->scanForRecovery();
    if (!m_excludeOk)
        log::warn("Windows did not accept capture exclusion for the main window; it will be minimised while "
                  "recording a screen");

    // Check in the background which library files still exist.
    QPointer<MainWindow> self(this);
    const QList<RecordingEntry> entries = m_history.entries();
    (void)QtConcurrent::run([self, entries] {
        QList<RecordingEntry> checked = entries;
        for (RecordingEntry& e : checked)
            e.exists = QFileInfo::exists(e.path);
        QMetaObject::invokeMethod(qApp, [self, checked] {
            if (!self)
                return;
            self->m_history.setEntries(checked);
            self->m_recordPage->refreshRecent();
            self->m_libraryPage->refresh();
        });
    });

    showPage(RecordPg);
    if (m_automation.selfTestSeconds > 0)
        QTimer::singleShot(2500, this, &MainWindow::runSelfTest);
    else if (!m_automation.snapshotDir.isEmpty())
        QTimer::singleShot(1500, this, &MainWindow::runSnapshots);
}

MainWindow::~MainWindow()
{
    m_store->saveNow();
    // Order matters: the preview controller stops its idle session, then the recording
    // controller finalises any running recording - both before the webcam controller
    // (a session may reference the camera's frame exchange).
    delete m_preview;
    m_preview = nullptr;
    delete m_controller;
    m_controller = nullptr;
    delete m_hud;
}

// ------------------------------------------------------------------- layout

QWidget* MainWindow::buildTopBar()
{
    auto* bar = new QFrame(this);
    bar->setObjectName("TopBar");
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(16, 8, 16, 8);
    h->setSpacing(10);
    auto* logo = new QLabel(bar);
    logo->setPixmap(makeIcon(IconId::App, Qt::white).pixmap(30, 30));
    auto* names = new QVBoxLayout;
    names->setSpacing(0);
    auto* brand = new QLabel(QStringLiteral("LumaCapture <span style='font-size:8pt; color:#9AA3B2'>v2</span>"), bar);
    brand->setObjectName("Brand");
    brand->setTextFormat(Qt::RichText);
    auto* sub = new QLabel(QStringLiteral("Lightweight Screen Recorder"), bar);
    sub->setObjectName("BrandSub");
    names->addWidget(brand);
    names->addWidget(sub);
    h->addWidget(logo);
    h->addLayout(names);
    h->addSpacing(28);

    m_nav = new QButtonGroup(bar);
    m_nav->setExclusive(true);
    struct Item {
        Page page;
        IconId icon;
        const char* text;
        const char* shortcut;
    };
    const Item items[] = {{RecordPg, IconId::Record, "Record", "Ctrl+1"},
                          {LibraryPg, IconId::Library, "Library", "Ctrl+2"},
                          {SettingsPg, IconId::Settings, "Settings", "Ctrl+,"},
                          {AboutPg, IconId::Info, "Help / About", "F1"}};
    for (const Item& it : items) {
        auto* b = new QPushButton(makeIcon(it.icon, it.page == RecordPg ? currentPalette().record : currentPalette().text),
                                  QStringLiteral("  ") + QString::fromLatin1(it.text), bar);
        b->setObjectName("Nav");
        b->setCheckable(true);
        b->setShortcut(QKeySequence(QString::fromLatin1(it.shortcut)));
        b->setToolTip(QStringLiteral("%1 (%2)").arg(QString::fromLatin1(it.text), QString::fromLatin1(it.shortcut)));
        m_nav->addButton(b, static_cast<int>(it.page));
        h->addWidget(b);
    }
    h->addStretch();
    m_topState = new QLabel(bar);
    m_topState->setObjectName("Pill");
    m_topState->hide();
    h->addWidget(m_topState);
    connect(m_nav, &QButtonGroup::idClicked, this, [this](int id) { showPage(static_cast<Page>(id)); });
    return bar;
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(makeIcon(IconId::App, Qt::white), this);
    m_tray->setToolTip(QStringLiteral("LumaCapture"));
    auto* menu = new QMenu(this);
    m_trayRecord = menu->addAction(QStringLiteral("Start recording"), this, &MainWindow::onRecordClicked);
    menu->addAction(QStringLiteral("Pause / resume"), m_controller, &RecordingController::togglePause);
    menu->addAction(QStringLiteral("Take screenshot"), m_controller, &RecordingController::takeScreenshot);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Open library"), this, [this] {
        bringToFront();
        showPage(LibraryPg);
    });
    menu->addAction(QStringLiteral("Show LumaCapture"), this, &MainWindow::bringToFront);
    menu->addAction(QStringLiteral("Quit"), this, &QWidget::close);
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
            bringToFront();
    });
    m_tray->show();
}

void MainWindow::showPage(Page p)
{
    m_pages->setCurrentIndex(static_cast<int>(p));
    if (QAbstractButton* b = m_nav->button(static_cast<int>(p)))
        b->setChecked(true);
    updatePreviewVisibility();
}

void MainWindow::bringToFront()
{
    showNormal();
    raise();
    activateWindow();
}

bool MainWindow::exclude(QWidget* w)
{
    return setExcludedFromCapture(w, m_store->get().excludeOwnWindows);
}

void MainWindow::updatePreviewVisibility()
{
    const bool recordVisible = isVisible() && !isMinimized() && m_pages->currentIndex() == RecordPg;
    m_preview->setViewSize(m_recordPage->previewView()->pixelSize());
    m_preview->setVisible(recordVisible);
    m_recordPage->webcamPanel()->setActive(recordVisible && m_recordPage->currentContextTab() == RecordPage::WebcamTab);
    const AppSettings& s = m_store->get();
    if (s.diagnostics && recordVisible) {
        if (!m_diagTimer.isActive())
            m_diagTimer.start();
    } else {
        m_diagTimer.stop();
    }
}

void MainWindow::updateFooter()
{
    const QString dir = m_store->get().outputDir;
    m_footerPath->setText(QStringLiteral("Saving to %1").arg(QDir::toNativeSeparators(dir)));
    QPointer<MainWindow> self(this);
    (void)QtConcurrent::run([self, dir] {
        const int64_t free = freeDiskBytes(dir);
        QMetaObject::invokeMethod(qApp, [self, free] {
            if (self)
                self->m_footerFree->setText(
                    free >= 0 ? QStringLiteral("%1 free").arg(formatBytes(static_cast<uint64_t>(free))) : QString());
        });
    });
}

void MainWindow::notify(int kind, const QString& text, int autoHideMs)
{
    m_banner->showMessage(static_cast<Banner::Kind>(kind), text,
                          kind == 1 && !m_savedHint.isEmpty() ? QStringLiteral("Show in folder") : QString(), autoHideMs);
}

void MainWindow::showError(const QString& title, const QString& message)
{
    log::warn("{}: {}", title.toStdString(), message.toStdString());
    notify(3, QStringLiteral("<b>%1</b> - %2").arg(title.toHtmlEscaped(), message.toHtmlEscaped().replace('\n', QStringLiteral("<br>"))), 0);
}

// ----------------------------------------------------------------- settings

void MainWindow::onSettingsChanged(unsigned scope)
{
    const AppSettings& s = m_store->get();
    const bool capturing = m_controller->capturing();
    if (scope & SettingsStore::Appearance) {
        applyTheme(*qApp, s.theme);
        setDarkTitleBar(this, s.theme == Theme::Dark);
        notify(0, QStringLiteral("Theme changed. Some icons update after LumaCapture is restarted."), 6000);
    }
    if (scope & (SettingsStore::Webcam | SettingsStore::Overlays))
        applyWebcam(); // while recording only the filters change (the camera is locked)
    if (scope & SettingsStore::Hotkeys)
        registerHotkeys(true);
    if (scope & SettingsStore::General) {
        m_recordPage->setDiagnosticsVisible(s.diagnostics);
        m_preview->setEnabled(s.livePreview && !s.cpuConvert);
        exclude(this);
        updatePreviewVisibility();
    }
    if (scope & SettingsStore::Video)
        m_preview->setEnabled(s.livePreview && !s.cpuConvert);
    if (scope & SettingsStore::Output)
        updateFooter();
    if (capturing) {
        if (scope & (SettingsStore::Overlays | SettingsStore::Effects | SettingsStore::Audio | SettingsStore::Webcam))
            m_liveTimer.start();
        if (scope & SettingsStore::Audio) {
            m_controller->setMicMuted(s.micMuted);
            m_hud->setMicMuted(s.micMuted, s.microphone);
        }
    } else if (scope & (SettingsStore::Source | SettingsStore::Video | SettingsStore::Overlays | SettingsStore::Effects)) {
        m_preview->invalidate();
    }
}

void MainWindow::applyWebcam()
{
    // Preview frames are produced only while the Webcam tab is visible (setPreviewRequested);
    // the camera itself runs when the webcam is part of the recording.
    AppSettings copy = m_store->get();
    copy.webcamPreview = false;
    m_webcam->apply(copy);
}

void MainWindow::registerHotkeys(bool showConflicts)
{
    const QStringList conflicts = m_hotkeys->registerAll(m_store->get());
    m_settingsPage->setHotkeyConflicts(conflicts);
    if (!conflicts.isEmpty() && showConflicts)
        notify(2, QStringLiteral("Some hotkeys are unavailable: %1. Change them in Settings > Hotkeys.")
                      .arg(conflicts.join(QStringLiteral("; "))),
               0);
}

void MainWindow::setTestLevels(bool on)
{
    const AppSettings& s = m_store->get();
    if (on && !m_controller->busy())
        m_audioTest->start(s.systemAudio, s.systemDevice, s.microphone, s.micDevice);
    else
        m_audioTest->stop();
    const bool active = on && !m_controller->busy();
    m_recordPage->setTestLevelsActive(active);
    m_settingsPage->setTestLevelsActive(active);
}

void MainWindow::sampleDiagnostics()
{
    if (m_diagBusy)
        return;
    m_diagBusy = true;
    QPointer<MainWindow> self(this);
    const auto state = m_diag;
    (void)QtConcurrent::run([self, state] {
        DiagnosticsSample d;
        {
            std::lock_guard lock(state->mutex);
            if (!state->process)
                state->process = std::make_unique<ProcessMetrics>();
            if (!state->gpu)
                state->gpu = std::make_unique<GpuMetrics>();
            const ProcessSample p = state->process->sample();
            d.processCpu = p.processCpuPercent;
            d.systemCpu = p.systemCpuPercent;
            d.ramMb = static_cast<double>(p.workingSetBytes) / (1024.0 * 1024.0);
            if (const auto g = state->gpu->sample())
                d.gpu = g->busiestEnginePercent;
        }
        QMetaObject::invokeMethod(qApp, [self, d] {
            if (!self)
                return;
            self->m_diagBusy = false;
            self->m_recordPage->setDiagnostics(d);
        });
    });
}

// ---------------------------------------------------------------- recording

void MainWindow::onRecordClicked()
{
    if (m_controller->capturing())
        m_controller->stop();
    else if (!m_controller->busy())
        startWithPreflight();
    // Starting / Stopping / Finalizing: repeated clicks are ignored.
}

void MainWindow::startWithPreflight()
{
    const QString problem = m_controller->validateSource();
    if (!problem.isEmpty()) {
        showError(QStringLiteral("Cannot record"), problem);
        return;
    }
    // Free space is checked off the UI thread (a sleeping HDD can take seconds to answer).
    const QString dir = m_store->get().outputDir;
    const int64_t warnBytes = static_cast<int64_t>(m_store->get().lowSpaceWarnMB) * 1024 * 1024;
    QPointer<MainWindow> self(this);
    (void)QtConcurrent::run([self, dir, warnBytes] {
        const int64_t free = freeDiskBytes(dir);
        QMetaObject::invokeMethod(qApp, [self, free, warnBytes] {
            if (!self || self->m_controller->busy())
                return;
            if (free >= 0 && free < warnBytes && self->m_automation.selfTestSeconds <= 0) {
                QMessageBox box(QMessageBox::Warning, QStringLiteral("Low disk space"),
                                QStringLiteral("Only %1 is free on the recording drive. Record anyway?")
                                    .arg(formatBytes(static_cast<uint64_t>(free))),
                                QMessageBox::NoButton, self);
                auto* go = box.addButton(QStringLiteral("Record anyway"), QMessageBox::AcceptRole);
                box.addButton(QMessageBox::Cancel);
                self->exclude(&box);
                box.exec();
                if (box.clickedButton() != go)
                    return;
            }
            self->continueStart();
        });
    });
}

void MainWindow::continueStart()
{
    setTestLevels(false); // free the devices for the recording
    const AppSettings& s = m_store->get();
    if (s.countdownSeconds > 0) {
        QScreen* screen = QGuiApplication::primaryScreen();
        for (QScreen* sc : QGuiApplication::screens())
            if (sc->name() == s.displayName)
                screen = sc;
        auto* cd = new CountdownOverlay(screen, s.countdownSeconds);
        exclude(cd);
        connect(cd, &CountdownOverlay::finished, this, &MainWindow::beginRecording);
        connect(cd, &CountdownOverlay::cancelled, this, [this] { notify(0, QStringLiteral("Countdown cancelled"), 4000); });
        cd->show();
        cd->activateWindow();
        cd->setFocus();
    } else {
        beginRecording();
    }
}

void MainWindow::beginRecording()
{
    const AppSettings& s = m_store->get();
    // If Windows refused capture exclusion and the recording covers the screen this
    // window is on, get it out of the picture instead.
    const QRect area = m_controller->capturedScreenRect();
    const bool overlaps = !area.isEmpty() && area.intersects(frameGeometry());
    if (s.minimizeOnRecord || (s.excludeOwnWindows && !m_excludeOk && overlaps)) {
        m_restoreAfterRecording = !s.minimizeOnRecord;
        showMinimized();
    }
    // The idle preview releases the screen capture first, then the recording starts.
    m_preview->releaseForRecording([this] { m_controller->start(); });
}

void MainWindow::onStateChanged(session::RecState st)
{
    using S = session::RecState;
    const AppSettings& s = m_store->get();
    const bool busy = session::isBusy(st);
    m_webcam->setLocked(busy); // a session reads the camera's frames until the file is finalised
    m_recordPage->setRecordingState(st);
    m_settingsPage->setBusy(busy);
    m_libraryPage->setRecordingFile(busy ? m_controller->currentFile() : QString());

    if (st == S::Recording && !m_meterTimer.isActive()) {
        m_meterTimer.start();
        m_preview->setRecording(true, s.livePreview && s.previewWhileRecording);
        if (s.recordingHud && !m_hud->isVisible()) {
            const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
            m_hud->setMicMuted(s.micMuted, s.microphone);
            m_hud->adjustSize();
            m_hud->move(avail.center().x() - m_hud->width() / 2, avail.top() + 12);
            if (!exclude(m_hud))
                log::warn("Recording HUD: capture exclusion refused by Windows; the HUD stays hidden");
            else
                m_hud->show(); // excluded before it is first shown
        }
    }
    if (st == S::Stopping || st == S::Finalizing) {
        m_hud->hide();
        m_meterTimer.stop();
        m_recordPage->resetLevels();
        m_finishTicker.start();
    } else {
        m_finishTicker.stop();
    }
    if (st == S::Idle || st == S::Error) {
        m_hud->hide();
        m_meterTimer.stop();
        m_preview->setRecording(false, true);
        applyWebcam(); // apply camera changes made during the recording
        updateFooter();
        if (m_restoreAfterRecording) {
            m_restoreAfterRecording = false;
            showNormal();
        }
        if (m_quitAfterStop)
            QTimer::singleShot(0, this, &QWidget::close);
    }

    QString pill;
    const char* pillState = "idle";
    switch (st) {
    case S::Starting: pill = QStringLiteral("STARTING"); pillState = "busy"; break;
    case S::Recording: pill = QStringLiteral("● REC"); pillState = "rec"; break;
    case S::Paused: pill = QStringLiteral("PAUSED"); pillState = "paused"; break;
    case S::Stopping:
    case S::Finalizing: pill = QStringLiteral("SAVING"); pillState = "busy"; break;
    default: break;
    }
    m_topState->setText(pill);
    m_topState->setProperty("state", pillState);
    m_topState->style()->unpolish(m_topState);
    m_topState->style()->polish(m_topState);
    m_topState->setVisible(!pill.isEmpty());
    if (m_trayRecord)
        m_trayRecord->setText(session::isCapturing(st) ? QStringLiteral("Stop recording") : QStringLiteral("Start recording"));
    if (m_tray)
        m_tray->setToolTip(session::isCapturing(st) ? QStringLiteral("LumaCapture - recording") : QStringLiteral("LumaCapture"));
}

void MainWindow::onStatusTick()
{
    const session::SessionStatus& st = m_controller->lastStatus();
    m_recordPage->setStatus(st);
    const QString time = formatDuration(st.elapsedSeconds);
    m_hud->setElapsed(time, st.paused);
    if (m_topState->isVisible() && !st.paused)
        m_topState->setText(QStringLiteral("● REC  %1").arg(time));
}

void MainWindow::onHotkey(HotkeyAction a)
{
    switch (a) {
    case HotkeyAction::RecordToggle: onRecordClicked(); break;
    case HotkeyAction::Stop: m_controller->stop(); break;
    case HotkeyAction::PauseResume: m_controller->togglePause(); break;
    case HotkeyAction::MicMute:
        if (m_store->get().microphone)
            m_store->edit(SettingsStore::Audio, [](AppSettings& s) { s.micMuted = !s.micMuted; });
        break;
    case HotkeyAction::Screenshot: m_controller->takeScreenshot(); break;
    default: break;
    }
}

void MainWindow::selectRegion(double aspect)
{
    const AppSettings& s = m_store->get();
    QScreen* screen = QGuiApplication::primaryScreen();
    MonitorEntry mon;
    for (const MonitorEntry& m : m_scanner->monitors())
        if (m.deviceName == s.displayName || (s.displayName.isEmpty() && m.primary))
            mon = m;
    for (QScreen* sc : QGuiApplication::screens())
        if (sc->name() == mon.deviceName)
            screen = sc;
    if (!mon.rect.isValid() && !m_scanner->monitors().isEmpty())
        mon = m_scanner->monitors().first();
    if (!mon.rect.isValid())
        mon.rect = QRect(QPoint(0, 0), screen->size() * screen->devicePixelRatio());

    auto* sel = new RegionSelector(screen, mon.rect, aspect);
    connect(sel, &RegionSelector::selected, this, [this](const QRect& r) {
        m_store->edit(SettingsStore::Source, [r](AppSettings& st) {
            st.region = r;
            st.sourceKind = SourceKind::Region;
        });
        bringToFront();
    });
    connect(sel, &RegionSelector::cancelled, this, &MainWindow::bringToFront);
    showMinimized();
    sel->show();
    sel->activateWindow();
    sel->setFocus();
}

void MainWindow::openLayoutEditor()
{
    if (m_layoutDialog) {
        m_layoutDialog->raise();
        m_layoutDialog->activateWindow();
        return;
    }
    // Background: a small grab of the selected display (for orientation only).
    const AppSettings& s = m_store->get();
    QPixmap background;
    if (s.sourceKind == SourceKind::Display && m_automation.snapshotDir.isEmpty()) {
        for (QScreen* sc : QGuiApplication::screens())
            if (sc->name() == s.displayName || (s.displayName.isEmpty() && sc == QGuiApplication::primaryScreen()))
                background = sc->grabWindow(0).scaledToWidth(960, Qt::SmoothTransformation);
    }
    auto* dlg = new LayoutDialog(m_store->ref(), *m_webcam, m_controller->outputSize(), background, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_layoutDialog = dlg;
    connect(dlg, &LayoutDialog::changed, this,
            [this] { m_store->notify(SettingsStore::Overlays | SettingsStore::Webcam); });
    exclude(dlg);
    dlg->show();
}

// ------------------------------------------------------------------- events

void MainWindow::showEvent(QShowEvent* e)
{
    QMainWindow::showEvent(e);
    if (!m_hotkeysRegistered) {
        m_hotkeysRegistered = true;
        m_hotkeys->setWindow(static_cast<quintptr>(winId()));
        registerHotkeys(m_automation.selfTestSeconds <= 0 && m_automation.snapshotDir.isEmpty());
    }
    QTimer::singleShot(0, this, &MainWindow::updatePreviewVisibility);
}

void MainWindow::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange) {
        updatePreviewVisibility(); // a minimised window does not need the preview
        if (isMinimized() && m_store->get().minimizeToTray && m_tray)
            QTimer::singleShot(0, this, &QWidget::hide);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (!droppedVideos(e->mimeData()).isEmpty())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
    const QStringList files = droppedVideos(e->mimeData());
    if (files.isEmpty())
        return;
    e->acceptProposedAction();
    showPage(LibraryPg);
    m_libraryPage->importFiles(files);
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    const auto* msg = static_cast<const MSG*>(message);
    if (msg && msg->message == activateMessageId()) {
        bringToFront();
        *result = 0;
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_controller->busy()) {
        if (!m_quitAfterStop) {
            QMessageBox box(QMessageBox::Question, QStringLiteral("Recording in progress"),
                            QStringLiteral("A recording is running. Stop it, save the file and quit?"),
                            QMessageBox::NoButton, this);
            auto* yes = box.addButton(QStringLiteral("Stop, save and quit"), QMessageBox::AcceptRole);
            box.addButton(QMessageBox::Cancel);
            exclude(&box);
            box.exec();
            if (box.clickedButton() != yes) {
                e->ignore();
                return;
            }
            m_quitAfterStop = true;
            m_controller->stop();
        }
        e->ignore(); // closes itself once the file is saved
        return;
    }
    m_store->saveNow(); // written on the disk worker; main() waits a bounded time for it
    m_hotkeys->unregisterAll();
    m_audioTest->stop();
    m_webcam->stopCamera();
    if (m_tray)
        m_tray->hide();
    e->accept();
    qApp->quit();
}

// --------------------------------------------------------------- automation

void MainWindow::runSelfTest()
{
    // Scripted GUI check (used by scripts/test.ps1): record, pause, resume, stop,
    // repeated/invalid requests, webcam on/off, settings page, then quit. Writes only
    // into the output directory given on the command line.
    auto logStep = [](const QString& s) { log::info("SELFTEST: {}", s.toStdString()); };
    auto fail = [logStep](const QString& why) {
        logStep(QStringLiteral("FAIL - ") + why);
        QApplication::exit(1);
    };
    QTimer::singleShot(180000, this, [fail] { fail(QStringLiteral("overall timeout (180 s)")); });

    const int half = std::max(1, m_automation.selfTestSeconds / 2) * 1000;
    logStep(QStringLiteral("start recording"));
    m_preview->releaseForRecording([this] {
        m_controller->start();
        m_controller->start(); // must be ignored (already starting)
    });

    auto* tick = new QTimer(this);
    tick->setInterval(100);
    auto phase = std::make_shared<int>(0);
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    connect(tick, &QTimer::timeout, this, [this, phase, clock, half, logStep, fail, tick] {
        const auto st = m_controller->state();
        const qint64 t = clock->elapsed();
        auto next = [&] {
            ++*phase;
            clock->restart();
        };
        switch (*phase) {
        case 0:
            if (st == session::RecState::Recording) {
                logStep(QStringLiteral("recording"));
                next();
            } else if (st == session::RecState::Error || t > 15000) {
                tick->stop();
                fail(QStringLiteral("recording did not start"));
            }
            break;
        case 1:
            if (t > half) {
                logStep(QStringLiteral("pause"));
                m_controller->togglePause();
                next();
            }
            break;
        case 2:
            if (t > 1000) {
                logStep(QStringLiteral("resume"));
                m_controller->togglePause();
                next();
            }
            break;
        case 3:
            if (t > half) {
                logStep(QStringLiteral("stop (twice, second must be ignored)"));
                m_controller->stop();
                m_controller->stop();
                next();
            }
            break;
        case 4:
            if (st == session::RecState::Idle) {
                const QString f = m_controller->lastFile();
                logStep(QStringLiteral("saved %1 (%2 bytes)").arg(f).arg(QFileInfo(f).size()));
                if (!QFileInfo::exists(f)) {
                    tick->stop();
                    fail(QStringLiteral("no file saved"));
                    return;
                }
                next();
            } else if (st == session::RecState::Error || t > 60000) {
                tick->stop();
                fail(QStringLiteral("stop did not finish"));
            }
            break;
        case 5:
            if (m_scanner->cameras().isEmpty()) {
                logStep(QStringLiteral("no camera - skipping webcam steps"));
                *phase = 9;
                clock->restart();
                break;
            }
            logStep(QStringLiteral("webcam on"));
            m_store->edit(SettingsStore::Webcam | SettingsStore::Overlays, [this](AppSettings& s) {
                if (s.webcamLink.isEmpty()) {
                    s.webcamLink = m_scanner->cameras().first().link;
                    s.webcamName = m_scanner->cameras().first().name;
                }
                s.webcamEnabled = true;
            });
            next();
            break;
        case 6:
            if (t > 3000) {
                logStep(QStringLiteral("webcam off (v1 hung the UI here)"));
                m_store->edit(SettingsStore::Webcam | SettingsStore::Overlays, [](AppSettings& s) { s.webcamEnabled = false; });
                next();
            }
            break;
        case 7:
            if (t > 1500) {
                logStep(QStringLiteral("open the webcam tab"));
                m_recordPage->showContextTab(RecordPage::WebcamTab);
                next();
            }
            break;
        case 8:
            if (t > 4000) {
                logStep(QStringLiteral("leave the webcam tab, open settings"));
                m_recordPage->showContextTab(RecordPage::VideoTab);
                showPage(SettingsPg);
                next();
            }
            break;
        case 9:
            if (t > 1500) {
                tick->stop();
                logStep(QStringLiteral("PASS"));
                QApplication::exit(0);
            }
            break;
        }
    });
    tick->start();
}

void MainWindow::runSnapshots()
{
    // Renders LumaCapture's own pages to PNG (QWidget::grab - no screen capture).
    const QString dir = m_automation.snapshotDir;
    QDir().mkpath(dir);
    const struct {
        Page page;
        const char* name;
    } pages[] = {{RecordPg, "record"}, {LibraryPg, "library"}, {AboutPg, "about"}};
    for (const auto& p : pages) {
        showPage(p.page);
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/%1.png").arg(QString::fromLatin1(p.name)));
    }
    showPage(RecordPg);
    for (int tab = 0; tab < 5; ++tab) {
        m_recordPage->showContextTab(static_cast<RecordPage::Tab>(tab));
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/record-tab-%1.png").arg(tab));
    }
    showPage(SettingsPg);
    for (int section = 0; section <= SettingsPage::About; ++section) {
        m_settingsPage->showSection(static_cast<SettingsPage::Section>(section));
        QApplication::processEvents();
        grab().save(dir + QStringLiteral("/settings-%1.png").arg(section));
    }
    openLayoutEditor();
    QApplication::processEvents();
    if (m_layoutDialog)
        m_layoutDialog->grab().save(dir + QStringLiteral("/layout.png"));
    log::info("UI snapshots written to {}", dir.toStdString());
    QTimer::singleShot(500, this, [] { QApplication::exit(0); });
}

} // namespace luma::app
