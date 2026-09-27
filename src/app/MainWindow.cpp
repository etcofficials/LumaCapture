#include "MainWindow.h"

#include "AppPaths.h"
#include "AudioMonitor.h"
#include "CrashHandler.h"
#include "HotkeyManager.h"
#include "Icons.h"
#include "LayoutDialog.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "WebcamController.h"
#include "WinUtil.h"
#include "util/Log.h"
#include "widgets/Widgets.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace luma::app {
namespace {

constexpr int kRolePath = Qt::UserRole;
constexpr int kRoleMeta = Qt::UserRole + 1;
constexpr int kRoleMissing = Qt::UserRole + 2;

QFrame* card(const QString& title, QWidget* parent, QVBoxLayout*& body)
{
    auto* f = new QFrame(parent);
    f->setObjectName("Card");
    body = new QVBoxLayout(f);
    body->setContentsMargins(16, 14, 16, 16);
    body->setSpacing(10);
    if (!title.isEmpty()) {
        auto* t = new QLabel(title.toUpper(), f);
        t->setObjectName("SectionTitle");
        body->addWidget(t);
    }
    return f;
}

QLabel* hint(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName("Hint");
    l->setWordWrap(true);
    return l;
}

// Two-line history row: file name + "date  •  duration  •  resolution  •  size".
class RecentDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {200, 54}; }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const override
    {
        const Palette& pal = currentPalette();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(opt.rect).adjusted(2, 2, -2, -2);
        if (opt.state & QStyle::State_Selected)
            p->setBrush(currentPalette().accent.darker(opt.palette.window().color().lightness() > 128 ? 90 : 250));
        else if (opt.state & QStyle::State_MouseOver)
            p->setBrush(pal.surfaceAlt);
        else
            p->setBrush(Qt::NoBrush);
        p->setPen(opt.state & QStyle::State_Selected ? QPen(pal.accent, 1) : Qt::NoPen);
        p->drawRoundedRect(r, 8, 8);

        const bool missing = index.data(kRoleMissing).toBool();
        const QRectF badge(r.left() + 10, r.center().y() - 16, 32, 32);
        p->setPen(Qt::NoPen);
        p->setBrush(pal.surfaceAlt);
        p->drawRoundedRect(badge, 8, 8);
        makeIcon(IconId::Play, missing ? pal.textDim : pal.accent).paint(p, badge.adjusted(8, 8, -8, -8).toRect());

        QFont f = opt.font;
        f.setWeight(QFont::DemiBold);
        p->setFont(f);
        p->setPen(missing ? pal.textDim : pal.text);
        const QRectF textR(badge.right() + 12, r.top() + 7, r.width() - badge.width() - 34, 20);
        p->drawText(textR, Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle,
                                                static_cast<int>(textR.width())));
        f.setWeight(QFont::Normal);
        f.setPointSizeF(f.pointSizeF() - 0.8);
        p->setFont(f);
        p->setPen(pal.textDim);
        p->drawText(textR.translated(0, 20), Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(index.data(kRoleMeta).toString(), Qt::ElideRight,
                                                static_cast<int>(textR.width())));
        p->restore();
    }
};

} // namespace

MainWindow::MainWindow(const AutomationOptions& automation, QWidget* parent)
    : QMainWindow(parent), m_automation(automation), m_history(AppPaths::historyFile())
{
    m_settings = loadSettings(AppPaths::settingsFile());
    m_history.load();

    setWindowTitle(QStringLiteral("LumaCapture"));
    setWindowIcon(makeIcon(IconId::App, Qt::white));
    resize(1180, 780);
    setMinimumSize(1000, 660);

    m_scanner = new DeviceScanner(this);
    m_webcam = new WebcamController(this);
    m_controller = new RecordingController(m_settings, *m_webcam, *m_scanner, m_history, this);
    m_hotkeys = new HotkeyManager(this);
    m_audioTest = new AudioMonitor(this);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(18, 14, 18, 10);
    root->setSpacing(14);

    // Header
    auto* header = new QHBoxLayout;
    header->setSpacing(10);
    auto* logo = new QLabel(central);
    logo->setPixmap(makeIcon(IconId::App, Qt::white).pixmap(26, 26));
    header->addWidget(logo);
    auto* brand = new QLabel(QStringLiteral("LumaCapture"), central);
    brand->setObjectName("Brand");
    header->addWidget(brand);
    header->addStretch();
    auto* profileLabel = new QLabel(QStringLiteral("Profile"), central);
    profileLabel->setObjectName("Dim");
    header->addWidget(profileLabel);
    m_profile = new QComboBox(central);
    m_profile->addItems(profileNames());
    m_profile->setMinimumWidth(160);
    m_profile->setToolTip(QStringLiteral("A profile sets several options at once; you can still change each of them"));
    m_profile->setAccessibleName(QStringLiteral("Profile"));
    header->addWidget(m_profile);
    auto* settingsBtn = new QPushButton(makeIcon(IconId::Settings, currentPalette().text), QStringLiteral("Settings"), central);
    settingsBtn->setToolTip(QStringLiteral("All settings (Ctrl+,)"));
    settingsBtn->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(settingsBtn, &QPushButton::clicked, this, [this] { openSettings(SettingsDialog::General); });
    header->addWidget(settingsBtn);
    root->addLayout(header);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(16);
    auto* leftScroll = new QScrollArea(central);
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* leftW = new QWidget(leftScroll);
    auto* left = new QVBoxLayout(leftW);
    left->setContentsMargins(0, 0, 8, 0);
    left->setSpacing(14);
    left->addWidget(buildSourceCard());
    left->addWidget(buildVideoCard());
    left->addWidget(buildAudioCard());
    left->addWidget(buildWebcamCard());
    left->addStretch();
    leftScroll->setWidget(leftW);
    leftScroll->setMinimumWidth(440);
    leftScroll->setMaximumWidth(520);
    columns->addWidget(leftScroll, 5);

    auto* right = new QVBoxLayout;
    right->setSpacing(14);
    right->addWidget(buildStudioPanel());
    right->addWidget(buildRecentPanel(), 1);
    columns->addLayout(right, 7);
    root->addLayout(columns, 1);
    setCentralWidget(central);

    m_footerPath = new QLabel(this);
    m_footerPath->setObjectName("Dim");
    m_footerFree = new QLabel(this);
    m_footerFree->setObjectName("Dim");
    statusBar()->addWidget(m_footerPath, 1);
    statusBar()->addPermanentWidget(m_footerFree);
    statusBar()->setSizeGripEnabled(true);

    m_bar = new RecordingBar(nullptr);
    connect(m_bar, &RecordingBar::pauseClicked, m_controller, &RecordingController::togglePause);
    connect(m_bar, &RecordingBar::stopClicked, m_controller, &RecordingController::stop);
    connect(m_bar, &RecordingBar::micClicked, this, [this] { m_micMute->click(); });

    buildTray();

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(600);
    connect(&m_saveTimer, &QTimer::timeout, this, &MainWindow::saveSettingsNow);
    m_liveTimer.setSingleShot(true);
    m_liveTimer.setInterval(150);
    connect(&m_liveTimer, &QTimer::timeout, m_controller, &RecordingController::updateLive);
    m_heartbeat.setInterval(500);
    connect(&m_heartbeat, &QTimer::timeout, this, [] { crash::heartbeat(); });
    m_heartbeat.start();
    m_finishTicker.setInterval(500);
    connect(&m_finishTicker, &QTimer::timeout, this, [this] {
        const double s = m_controller->finishingSeconds();
        const bool finalizing = m_controller->state() == session::RecState::Finalizing;
        QString text = finalizing ? QStringLiteral("Finalising the file (%1 s)...").arg(s, 0, 'f', 0)
                                  : QStringLiteral("Stopping and saving (%1 s)...").arg(s, 0, 'f', 0);
        if (s > 20)
            text += QStringLiteral("  Large files or MP4 conversion on a slow disk can take a while.");
        m_stateText->setText(text);
    });

    connect(m_controller, &RecordingController::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_controller, &RecordingController::statusTick, this, &MainWindow::onStatusTick);
    connect(m_controller, &RecordingController::errorOccurred, this, &MainWindow::showError);
    connect(m_controller, &RecordingController::info, this, [this](const QString& m) { notify(0, m); });
    connect(m_controller, &RecordingController::historyChanged, this, &MainWindow::refreshRecent);
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
    connect(m_controller, &RecordingController::recoveryFound, this, [this](const QList<RecoveryCandidate>& items) {
        if (!m_automation.snapshotDir.isEmpty() || m_automation.selfTestSeconds > 0)
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
        box.exec();
        if (box.clickedButton() == recover)
            m_controller->recover(items);
    });
    connect(m_scanner, &DeviceScanner::monitorsReady, this, &MainWindow::fillMonitors);
    connect(m_scanner, &DeviceScanner::windowsReady, this, &MainWindow::fillWindows);
    connect(m_scanner, &DeviceScanner::audioReady, this, [this] { updateSummary(); });
    connect(m_webcam, &WebcamController::previewFrame, this, [this](const QImage& img) {
        if (m_settings.webcamPreview)
            m_webcamPreview->setFrame(img);
    });
    connect(m_webcam, &WebcamController::stateChanged, this, [this] {
        m_webcamState->setText(m_settings.webcamEnabled ? m_webcam->stateText() : QStringLiteral("Off"));
        if (!m_webcam->running())
            m_webcamPreview->setMessage(m_settings.webcamEnabled ? m_webcam->stateText()
                                                                 : QStringLiteral("Webcam overlay is off"));
    });
    connect(m_audioTest, &AudioMonitor::levels, this, [this](float s, float m) {
        m_systemMeter->setLevel(s);
        m_micMeter->setLevel(m);
    });
    connect(m_banner, &Banner::actionClicked, this, [this] {
        if (!m_savedHint.isEmpty())
            showInExplorer(m_savedHint);
    });
    connect(m_hotkeys, &HotkeyManager::triggered, this, &MainWindow::onHotkey);
    connect(qApp, &QGuiApplication::screenAdded, m_scanner, &DeviceScanner::scanMonitors);
    connect(qApp, &QGuiApplication::screenRemoved, m_scanner, &DeviceScanner::scanMonitors);

    if (!m_automation.outputDir.isEmpty()) {
        m_settings.outputDir = m_automation.outputDir;
        m_settings.countdownSeconds = 0;
        m_settings.minimizeOnRecord = false;
    }

    syncWidgetsFromSettings();
    m_scanner->scanMonitors();
    m_scanner->scanCameras();
    m_scanner->scanAudio();
    if (m_settings.sourceKind == SourceKind::Window)
        m_scanner->scanWindows();
    m_webcam->apply(m_settings);
    refreshRecent();
    updateRecordingUi();
    updateFooter();
    if (m_automation.selfTestSeconds <= 0 && m_automation.snapshotDir.isEmpty())
        m_controller->scanForRecovery();

    // Check in the background which history files still exist.
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
            self->refreshRecent();
        });
    });

    m_record->setFocus();
    if (m_automation.selfTestSeconds > 0)
        QTimer::singleShot(2500, this, &MainWindow::runSelfTest);
    else if (!m_automation.snapshotDir.isEmpty())
        QTimer::singleShot(1500, this, &MainWindow::runSnapshots);
}

MainWindow::~MainWindow()
{
    saveSettingsNow();
    // Destroy the recording controller (which finalises any running session) before the
    // webcam controller whose frame exchange a session may reference.
    delete m_controller;
    m_controller = nullptr;
    delete m_bar;
}

void MainWindow::saveSettingsNow()
{
    if (m_automation.selfTestSeconds > 0 || !m_automation.snapshotDir.isEmpty())
        return; // automation runs never change the user's settings
    saveSettings(m_settings, AppPaths::settingsFile());
}

void MainWindow::showEvent(QShowEvent* e)
{
    QMainWindow::showEvent(e);
    applyCaptureExclusion(this);
    if (!m_hotkeysRegistered) {
        m_hotkeysRegistered = true;
        m_hotkeys->setWindow(static_cast<quintptr>(winId()));
        registerHotkeys(m_automation.selfTestSeconds <= 0 && m_automation.snapshotDir.isEmpty());
    }
}

void MainWindow::applyCaptureExclusion(QWidget* w)
{
    setExcludedFromCapture(w, m_settings.excludeOwnWindows);
}

void MainWindow::registerHotkeys(bool showConflicts)
{
    const QStringList conflicts = m_hotkeys->registerAll(m_settings);
    if (!conflicts.isEmpty() && showConflicts)
        notify(2, QStringLiteral("Some hotkeys are unavailable: %1. Change them in Settings > Hotkeys.")
                      .arg(conflicts.join(QStringLiteral("; "))),
               0);
}

void MainWindow::notify(int kind, const QString& text, int autoHideMs)
{
    m_banner->showMessage(static_cast<Banner::Kind>(kind), text,
                          kind == 1 && !m_savedHint.isEmpty() ? QStringLiteral("Show in folder") : QString(), autoHideMs);
}

// ------------------------------------------------------------------ cards

QWidget* MainWindow::buildSourceCard()
{
    QVBoxLayout* body = nullptr;
    QFrame* c = card(QStringLiteral("Capture source"), this, body);
    m_sourceCard = c;

    auto* seg = new QHBoxLayout;
    seg->setSpacing(0);
    m_sourceGroup = new QButtonGroup(c);
    const struct { IconId icon; const char* text; const char* tip; } items[] = {
        {IconId::Display, "Display", "Record a whole monitor"},
        {IconId::Window, "Window", "Record one application window"},
        {IconId::Region, "Region", "Record a rectangle of the screen"}};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QPushButton(makeIcon(items[i].icon, currentPalette().text), QString::fromLatin1(items[i].text), c);
        b->setCheckable(true);
        b->setObjectName(i == 0 ? "SegFirst" : (i == 2 ? "SegLast" : "Seg"));
        b->setToolTip(QString::fromLatin1(items[i].tip));
        b->setAccessibleName(QStringLiteral("Capture %1").arg(QString::fromLatin1(items[i].text)));
        m_sourceGroup->addButton(b, i);
        seg->addWidget(b, 1);
    }
    body->addLayout(seg);

    m_sourceStack = new QStackedWidget(c);
    auto* dp = new QWidget(m_sourceStack);
    auto* dl = new QVBoxLayout(dp);
    dl->setContentsMargins(0, 0, 0, 0);
    m_display = new QComboBox(dp);
    m_display->setAccessibleName(QStringLiteral("Display"));
    dl->addWidget(m_display);
    m_sourceStack->addWidget(dp);

    auto* wp = new QWidget(m_sourceStack);
    auto* wl = new QHBoxLayout(wp);
    wl->setContentsMargins(0, 0, 0, 0);
    m_window = new QComboBox(wp);
    m_window->setAccessibleName(QStringLiteral("Window"));
    m_window->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_window->setMinimumContentsLength(18);
    auto* refresh = new QToolButton(wp);
    refresh->setIcon(makeIcon(IconId::Refresh, currentPalette().text));
    refresh->setToolTip(QStringLiteral("Refresh the window list"));
    refresh->setAccessibleName(QStringLiteral("Refresh window list"));
    wl->addWidget(m_window, 1);
    wl->addWidget(refresh);
    m_sourceStack->addWidget(wp);
    connect(refresh, &QToolButton::clicked, this, [this] { m_scanner->scanWindows(); });

    auto* rp = new QWidget(m_sourceStack);
    auto* rl = new QGridLayout(rp);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setHorizontalSpacing(8);
    auto* pick = new QPushButton(makeIcon(IconId::Region, currentPalette().text), QStringLiteral("Select on screen..."), rp);
    rl->addWidget(pick, 0, 0, 1, 2);
    m_regionAspect = new QComboBox(rp);
    m_regionAspect->addItems({QStringLiteral("Free aspect"), QStringLiteral("16:9"), QStringLiteral("4:3"),
                              QStringLiteral("1:1"), QStringLiteral("9:16")});
    m_regionAspect->setToolTip(QStringLiteral("Lock the aspect ratio while dragging"));
    rl->addWidget(m_regionAspect, 0, 2, 1, 2);
    auto spin = [rp](const QString& name) {
        auto* s = new QSpinBox(rp);
        s->setRange(-10000, 20000);
        s->setAccessibleName(name);
        return s;
    };
    m_regionX = spin(QStringLiteral("Region X"));
    m_regionY = spin(QStringLiteral("Region Y"));
    m_regionW = spin(QStringLiteral("Region width"));
    m_regionH = spin(QStringLiteral("Region height"));
    m_regionW->setMinimum(32);
    m_regionH->setMinimum(32);
    const char* labels[] = {"X", "Y", "W", "H"};
    QSpinBox* spins[] = {m_regionX, m_regionY, m_regionW, m_regionH};
    for (int i = 0; i < 4; ++i) {
        auto* l = new QLabel(QString::fromLatin1(labels[i]), rp);
        l->setObjectName("Dim");
        rl->addWidget(l, 1 + i / 2, (i % 2) * 2);
        rl->addWidget(spins[i], 1 + i / 2, (i % 2) * 2 + 1);
    }
    m_regionLabel = hint(QStringLiteral("Physical pixels; the region must lie inside one monitor."), rp);
    rl->addWidget(m_regionLabel, 3, 0, 1, 4);
    m_sourceStack->addWidget(rp);
    connect(pick, &QPushButton::clicked, this, &MainWindow::selectRegion);
    for (QSpinBox* s : spins)
        connect(s, &QSpinBox::valueChanged, this, [this] {
            if (m_loadingUi)
                return;
            m_settings.region = QRect(m_regionX->value(), m_regionY->value(), m_regionW->value() & ~1,
                                      m_regionH->value() & ~1);
            settingsChanged(false);
        });
    body->addWidget(m_sourceStack);
    // A QStackedWidget is as tall as its tallest page; let only the current page count.
    auto fitStack = [this](int index) {
        for (int i = 0; i < m_sourceStack->count(); ++i)
            m_sourceStack->widget(i)->setSizePolicy(QSizePolicy::Preferred,
                                                    i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        m_sourceStack->adjustSize();
    };
    connect(m_sourceStack, &QStackedWidget::currentChanged, this, fitStack);
    fitStack(0);

    m_cursor = new QCheckBox(QStringLiteral("Record the mouse cursor"), c);
    body->addWidget(m_cursor);
    m_sourceWarning = new QLabel(c);
    m_sourceWarning->setObjectName("Warn");
    m_sourceWarning->setWordWrap(true);
    m_sourceWarning->hide();
    body->addWidget(m_sourceWarning);

    connect(m_sourceGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_settings.sourceKind = static_cast<SourceKind>(id);
        m_sourceStack->setCurrentIndex(id);
        if (id == static_cast<int>(SourceKind::Window))
            m_scanner->scanWindows();
        settingsChanged(false);
    });
    connect(m_display, &QComboBox::activated, this, [this](int i) {
        m_settings.displayName = m_display->itemData(i).toString();
        settingsChanged(false);
    });
    connect(m_window, &QComboBox::activated, this, [this](int i) {
        m_controller->setWindowTarget(m_window->itemData(i).value<quintptr>());
        m_settings.windowTitle = m_window->itemData(i, Qt::ToolTipRole).toString();
        settingsChanged(false);
    });
    connect(m_cursor, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.captureCursor = on;
        settingsChanged(false);
    });
    return c;
}

QWidget* MainWindow::buildVideoCard()
{
    QVBoxLayout* body = nullptr;
    QFrame* c = card(QStringLiteral("Video"), this, body);
    m_videoCard = c;
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    m_resolution = new QComboBox(c);
    m_resolution->addItems({QStringLiteral("Native size"), QStringLiteral("1080p"), QStringLiteral("900p"),
                            QStringLiteral("720p"), QStringLiteral("480p")});
    m_resolution->setToolTip(QStringLiteral("Output height. The source aspect ratio is kept; nothing is upscaled."));
    m_fps = new QComboBox(c);
    m_fps->addItems({QStringLiteral("24 fps"), QStringLiteral("30 fps"), QStringLiteral("60 fps")});
    m_quality = new QComboBox(c);
    m_quality->addItems({QStringLiteral("Small file"), QStringLiteral("Balanced"), QStringLiteral("High quality"),
                         QStringLiteral("Custom")});
    m_quality->setToolTip(QStringLiteral("Small file = CRF 28, Balanced = CRF 23, High quality = CRF 19"));
    m_speed = new QComboBox(c);
    m_speed->addItems({QStringLiteral("ultrafast"), QStringLiteral("superfast"), QStringLiteral("veryfast"),
                       QStringLiteral("faster")});
    m_speed->setToolTip(QStringLiteral("x264 encoder speed (software). \"ultrafast\" uses the least CPU and is "
                                       "recommended on this PC."));
    const char* captions[] = {"Resolution", "Frame rate", "Quality", "Encoder speed"};
    QWidget* combos[] = {m_resolution, m_fps, m_quality, m_speed};
    for (int i = 0; i < 4; ++i) {
        auto* l = new QLabel(QString::fromLatin1(captions[i]), c);
        l->setObjectName("Dim");
        grid->addWidget(l, (i / 2) * 2, i % 2);
        grid->addWidget(combos[i], (i / 2) * 2 + 1, i % 2);
        combos[i]->setAccessibleName(QString::fromLatin1(captions[i]));
    }
    body->addLayout(grid);
    m_videoWarning = new QLabel(c);
    m_videoWarning->setObjectName("Warn");
    m_videoWarning->setWordWrap(true);
    body->addWidget(m_videoWarning);

    connect(m_resolution, &QComboBox::activated, this, [this](int i) {
        m_settings.resolution = static_cast<ResolutionPreset>(i);
        settingsChanged(false);
    });
    connect(m_fps, &QComboBox::activated, this, [this](int i) {
        m_settings.fps = i == 0 ? 24 : (i == 1 ? 30 : 60);
        settingsChanged(false);
    });
    connect(m_quality, &QComboBox::activated, this, [this](int i) {
        m_settings.quality = static_cast<QualityPreset>(i);
        settingsChanged(false);
        if (i == static_cast<int>(QualityPreset::Custom))
            openSettings(SettingsDialog::Video);
    });
    connect(m_speed, &QComboBox::activated, this, [this](int) {
        m_settings.x264Preset = m_speed->currentText();
        settingsChanged(false);
    });
    return c;
}

QWidget* MainWindow::buildAudioCard()
{
    QVBoxLayout* body = nullptr;
    QFrame* c = card(QStringLiteral("Audio"), this, body);
    auto row = [c](IconId icon, ToggleSwitch*& sw, QLabel*& name, const QString& title, LevelMeter*& meter,
                   QSlider*& vol, const QString& volName) {
        auto* w = new QWidget(c);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(4);
        auto* ic = new QLabel(w);
        ic->setPixmap(makeIcon(icon, currentPalette().text).pixmap(18, 18));
        sw = new ToggleSwitch(w);
        sw->setAccessibleName(title);
        auto* t = new QLabel(title, w);
        name = new QLabel(w);
        name->setObjectName("Hint");
        meter = new LevelMeter(w);
        vol = new QSlider(Qt::Horizontal, w);
        vol->setRange(0, 200);
        vol->setFixedWidth(96);
        vol->setToolTip(QStringLiteral("%1 in the recording (100% = unchanged)").arg(volName));
        vol->setAccessibleName(volName);
        g->addWidget(ic, 0, 0);
        g->addWidget(t, 0, 1);
        g->addWidget(name, 1, 1, 1, 2);
        g->addWidget(sw, 0, 3);
        g->addWidget(meter, 2, 1, 1, 1);
        g->addWidget(vol, 2, 2, 1, 2);
        g->setColumnStretch(1, 1);
        return w;
    };
    body->addWidget(row(IconId::Speaker, m_systemAudio, m_systemName, QStringLiteral("System audio"), m_systemMeter,
                        m_systemVolume, QStringLiteral("System audio volume")));
    auto* micRow = row(IconId::Mic, m_mic, m_micName, QStringLiteral("Microphone"), m_micMeter, m_micVolume,
                       QStringLiteral("Microphone volume"));
    m_micMute = new QToolButton(micRow);
    m_micMute->setCheckable(true);
    m_micMute->setToolTip(QStringLiteral("Mute / unmute the microphone (Ctrl+Alt+M)"));
    m_micMute->setAccessibleName(QStringLiteral("Mute microphone"));
    static_cast<QGridLayout*>(micRow->layout())->addWidget(m_micMute, 0, 2, Qt::AlignRight);
    body->addWidget(micRow);

    auto* buttons = new QHBoxLayout;
    m_audioTestButton = new QPushButton(QStringLiteral("Test levels"), c);
    m_audioTestButton->setCheckable(true);
    m_audioTestButton->setToolTip(QStringLiteral("Show levels without recording (nothing is saved)"));
    auto* more = new QPushButton(QStringLiteral("Devices && processing..."), c);
    buttons->addWidget(m_audioTestButton);
    buttons->addWidget(more);
    buttons->addStretch();
    body->addLayout(buttons);

    connect(m_systemAudio, &ToggleSwitch::toggled, this, [this](bool on) {
        m_settings.systemAudio = on;
        settingsChanged(false);
    });
    connect(m_mic, &ToggleSwitch::toggled, this, [this](bool on) {
        m_settings.microphone = on;
        m_micMute->setEnabled(on);
        settingsChanged(false);
    });
    connect(m_systemVolume, &QSlider::valueChanged, this, [this](int v) {
        m_settings.systemVolume = v / 100.0;
        settingsChanged();
    });
    connect(m_micVolume, &QSlider::valueChanged, this, [this](int v) {
        m_settings.micVolume = v / 100.0;
        settingsChanged();
    });
    connect(m_micMute, &QToolButton::toggled, this, [this](bool muted) {
        m_micMute->setIcon(makeIcon(muted ? IconId::MicOff : IconId::Mic, currentPalette().text));
        m_controller->setMicMuted(muted);
        m_bar->setMicMuted(muted, m_settings.microphone);
        m_saveTimer.start();
    });
    connect(m_audioTestButton, &QPushButton::toggled, this, [this](bool on) {
        if (on && !m_controller->busy())
            m_audioTest->start(m_settings.systemAudio, m_settings.systemDevice, m_settings.microphone, m_settings.micDevice);
        else
            m_audioTest->stop();
    });
    connect(more, &QPushButton::clicked, this, [this] { openSettings(SettingsDialog::Audio); });
    return c;
}

QWidget* MainWindow::buildWebcamCard()
{
    QVBoxLayout* body = nullptr;
    QFrame* c = card(QStringLiteral("Webcam"), this, body);
    auto* top = new QGridLayout;
    top->setHorizontalSpacing(10);
    auto* ic = new QLabel(c);
    ic->setPixmap(makeIcon(IconId::Camera, currentPalette().text).pixmap(18, 18));
    m_webcamName = new QLabel(c);
    m_webcamState = new QLabel(c);
    m_webcamState->setObjectName("Hint");
    m_webcamOn = new ToggleSwitch(c);
    m_webcamOn->setAccessibleName(QStringLiteral("Show webcam in recording"));
    m_webcamOn->setToolTip(QStringLiteral("Show the webcam in the recording"));
    top->addWidget(ic, 0, 0);
    top->addWidget(m_webcamName, 0, 1);
    top->addWidget(m_webcamState, 1, 1);
    top->addWidget(m_webcamOn, 0, 2);
    top->setColumnStretch(1, 1);
    body->addLayout(top);
    m_webcamPreview = new WebcamPreview(c);
    m_webcamPreview->setMinimumHeight(170);
    body->addWidget(m_webcamPreview);
    auto* row = new QHBoxLayout;
    auto* adjust = new QPushButton(makeIcon(IconId::Camera, currentPalette().text), QStringLiteral("Image && camera..."), c);
    adjust->setToolTip(QStringLiteral("Auto adjust, presets, filters, anti-flicker and camera controls"));
    auto* layout = new QPushButton(makeIcon(IconId::Layout, currentPalette().text), QStringLiteral("Layout && overlays..."), c);
    layout->setToolTip(QStringLiteral("Position, size, shape and crop of the webcam; text and image overlays"));
    row->addWidget(adjust);
    row->addWidget(layout);
    row->addStretch();
    body->addLayout(row);

    connect(m_webcamOn, &ToggleSwitch::toggled, this, [this](bool on) {
        if (m_loadingUi)
            return;
        if (on && m_settings.webcamLink.isEmpty()) {
            if (!m_scanner->cameras().isEmpty()) {
                m_settings.webcamLink = m_scanner->cameras().first().link;
                m_settings.webcamName = m_scanner->cameras().first().name;
            } else {
                showError(QStringLiteral("No camera found"),
                          QStringLiteral("Connect a webcam, then choose it under Image & camera."));
                m_loadingUi = true;
                m_webcamOn->setChecked(false);
                m_loadingUi = false;
                return;
            }
        }
        m_settings.webcamEnabled = on;
        m_webcamName->setText(m_settings.webcamName.isEmpty() ? QStringLiteral("No camera selected") : m_settings.webcamName);
        if (!m_controller->busy())
            m_webcam->apply(m_settings); // non-blocking: the camera starts/stops on its own thread
        settingsChanged();
    });
    connect(adjust, &QPushButton::clicked, this, [this] { openSettings(SettingsDialog::Webcam); });
    connect(layout, &QPushButton::clicked, this, &MainWindow::openLayout);
    return c;
}

QWidget* MainWindow::buildStudioPanel()
{
    auto* f = new QFrame(this);
    f->setObjectName("Card");
    auto* v = new QVBoxLayout(f);
    v->setContentsMargins(22, 18, 22, 18);
    v->setSpacing(14);

    auto* top = new QHBoxLayout;
    m_pill = new QLabel(QStringLiteral("READY"), f);
    m_pill->setObjectName("Pill");
    m_pill->setAccessibleName(QStringLiteral("Recording state"));
    top->addWidget(m_pill, 0, Qt::AlignVCenter);
    top->addStretch();
    m_timer = new QLabel(QStringLiteral("00:00:00"), f);
    m_timer->setObjectName("Timer");
    m_timer->setAccessibleName(QStringLiteral("Recording time"));
    top->addWidget(m_timer);
    v->addLayout(top);
    m_stateText = new QLabel(QStringLiteral("Ready to record"), f);
    m_stateText->setObjectName("Dim");
    v->addWidget(m_stateText);

    auto* tiles = new QHBoxLayout;
    tiles->setSpacing(18);
    m_tileOutput = new StatTile(QStringLiteral("Output"), f);
    m_tileFps = new StatTile(QStringLiteral("Frame rate"), f);
    m_tileDropped = new StatTile(QStringLiteral("Dropped frames"), f);
    m_tileSize = new StatTile(QStringLiteral("File size"), f);
    for (StatTile* t : {m_tileOutput, m_tileFps, m_tileDropped, m_tileSize})
        tiles->addWidget(t, 1);
    v->addLayout(tiles);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    m_record = new QPushButton(makeIcon(IconId::Record, Qt::white), QStringLiteral("  Start recording"), f);
    m_record->setObjectName("Record");
    m_record->setIconSize(QSize(16, 16));
    m_record->setMinimumWidth(210);
    m_record->setAccessibleName(QStringLiteral("Start or stop recording"));
    m_record->setToolTip(QStringLiteral("Start / stop recording (Ctrl+Alt+R)"));
    m_pause = new QPushButton(makeIcon(IconId::Pause, currentPalette().text), QString(), f);
    m_pause->setObjectName("Round");
    m_pause->setIconSize(QSize(18, 18));
    m_pause->setToolTip(QStringLiteral("Pause / resume (Ctrl+Alt+P)"));
    m_pause->setAccessibleName(QStringLiteral("Pause or resume"));
    m_screenshot = new QPushButton(makeIcon(IconId::Screenshot, currentPalette().text), QString(), f);
    m_screenshot->setObjectName("Round");
    m_screenshot->setIconSize(QSize(18, 18));
    m_screenshot->setToolTip(QStringLiteral("Save a PNG screenshot of the source (Ctrl+Alt+X)"));
    m_screenshot->setAccessibleName(QStringLiteral("Take screenshot"));
    buttons->addWidget(m_record);
    buttons->addWidget(m_pause);
    buttons->addWidget(m_screenshot);
    buttons->addStretch();
    v->addLayout(buttons);

    m_summary = new QLabel(f);
    m_summary->setObjectName("Dim");
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    v->addWidget(m_summary);

    m_banner = new Banner(f);
    v->addWidget(m_banner);

    connect(m_record, &QPushButton::clicked, this, &MainWindow::onRecordClicked);
    connect(m_pause, &QPushButton::clicked, m_controller, &RecordingController::togglePause);
    connect(m_screenshot, &QPushButton::clicked, m_controller, &RecordingController::takeScreenshot);
    return f;
}

QWidget* MainWindow::buildRecentPanel()
{
    QVBoxLayout* body = nullptr;
    QFrame* c = card(QStringLiteral("Recent recordings"), this, body);
    m_recent = new QListWidget(c);
    m_recent->setAccessibleName(QStringLiteral("Recent recordings"));
    m_recent->setItemDelegate(new RecentDelegate(m_recent));
    m_recent->setMouseTracking(true);
    m_recent->setUniformItemSizes(true);
    body->addWidget(m_recent, 1);
    auto* row = new QHBoxLayout;
    m_openBtn = new QPushButton(makeIcon(IconId::Play, currentPalette().text), QStringLiteral("Play"), c);
    m_showBtn = new QPushButton(makeIcon(IconId::Folder, currentPalette().text), QStringLiteral("Show in folder"), c);
    auto* openFolder = new QPushButton(QStringLiteral("Open recordings folder"), c);
    m_deleteBtn = new QPushButton(makeIcon(IconId::Trash, currentPalette().text), QStringLiteral("Delete..."), c);
    row->addWidget(m_openBtn);
    row->addWidget(m_showBtn);
    row->addWidget(openFolder);
    row->addStretch();
    row->addWidget(m_deleteBtn);
    body->addLayout(row);

    auto current = [this]() -> QString {
        QListWidgetItem* it = m_recent->currentItem();
        return it ? it->data(kRolePath).toString() : QString();
    };
    auto updateButtons = [this, current] {
        const QString f = current();
        const bool exists = !f.isEmpty() && m_recent->currentItem() && !m_recent->currentItem()->data(kRoleMissing).toBool();
        m_openBtn->setEnabled(exists);
        m_showBtn->setEnabled(exists);
        m_deleteBtn->setEnabled(!f.isEmpty());
    };
    connect(m_recent, &QListWidget::currentItemChanged, this, updateButtons);
    connect(m_openBtn, &QPushButton::clicked, this, [current] {
        const QString f = current();
        if (!f.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(f));
    });
    connect(m_recent, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* it) {
        const QString f = it->data(kRolePath).toString();
        if (!f.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(f));
    });
    connect(m_showBtn, &QPushButton::clicked, this, [current] {
        const QString f = current();
        if (!f.isEmpty())
            showInExplorer(f);
    });
    connect(openFolder, &QPushButton::clicked, this, [this] {
        QDir().mkpath(m_settings.outputDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings.outputDir));
    });
    connect(m_deleteBtn, &QPushButton::clicked, this, [this, current] {
        const QString f = current();
        if (f.isEmpty())
            return;
        if (f == m_controller->currentFile()) {
            showError(QStringLiteral("File in use"), QStringLiteral("This file is being recorded right now."));
            return;
        }
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Delete recording"),
                        QStringLiteral("Move \"%1\" to the Recycle Bin?\n\n%2")
                            .arg(QFileInfo(f).fileName(), QDir::toNativeSeparators(QFileInfo(f).path())),
                        QMessageBox::NoButton, this);
        auto* del = box.addButton(QStringLiteral("Move to Recycle Bin"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != del)
            return;
        if (QFileInfo::exists(f) && !QFile::moveToTrash(f)) {
            showError(QStringLiteral("Delete failed"), QStringLiteral("The file could not be moved to the Recycle Bin. "
                                                                      "It may be open in another program."));
            return;
        }
        m_history.remove(f);
        refreshRecent();
    });
    updateButtons();
    return c;
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
    menu->addAction(QStringLiteral("Show LumaCapture"), this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    menu->addAction(QStringLiteral("Quit"), this, &QWidget::close);
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) {
            showNormal();
            raise();
            activateWindow();
        }
    });
    m_tray->show();
}

// --------------------------------------------------------------- settings

void MainWindow::settingsChanged(bool live)
{
    if (m_loadingUi)
        return;
    m_saveTimer.start();
    if (live && m_controller->capturing())
        m_liveTimer.start();
    updateSummary();
    updateWarnings();
    updateFooter();
}

void MainWindow::syncWidgetsFromSettings()
{
    m_loadingUi = true;
    const AppSettings& s = m_settings;
    m_sourceGroup->button(static_cast<int>(s.sourceKind))->setChecked(true);
    m_sourceStack->setCurrentIndex(static_cast<int>(s.sourceKind));
    m_regionX->setValue(s.region.x());
    m_regionY->setValue(s.region.y());
    m_regionW->setValue(s.region.width());
    m_regionH->setValue(s.region.height());
    m_cursor->setChecked(s.captureCursor);
    m_resolution->setCurrentIndex(static_cast<int>(s.resolution));
    m_fps->setCurrentIndex(s.fps == 24 ? 0 : (s.fps == 60 ? 2 : 1));
    m_quality->setCurrentIndex(static_cast<int>(s.quality));
    m_speed->setCurrentText(s.x264Preset);
    m_systemAudio->setChecked(s.systemAudio);
    m_mic->setChecked(s.microphone);
    m_systemVolume->setValue(static_cast<int>(s.systemVolume * 100));
    m_micVolume->setValue(static_cast<int>(s.micVolume * 100));
    m_micMute->setChecked(s.micMuted);
    m_micMute->setIcon(makeIcon(s.micMuted ? IconId::MicOff : IconId::Mic, currentPalette().text));
    m_micMute->setEnabled(s.microphone);
    m_webcamOn->setChecked(s.webcamEnabled);
    m_webcamName->setText(s.webcamName.isEmpty() ? QStringLiteral("No camera selected") : s.webcamName);
    m_webcamState->setText(s.webcamEnabled ? m_webcam->stateText() : QStringLiteral("Off"));
    m_webcamPreview->setMirror(s.webcamPlacement.mirror);
    if (!s.webcamEnabled)
        m_webcamPreview->setMessage(QStringLiteral("Webcam overlay is off"));
    else if (!s.webcamPreview)
        m_webcamPreview->setMessage(QStringLiteral("Preview turned off (the webcam is still recorded)"));
    const int p = m_profile->findText(s.profile);
    m_profile->setCurrentIndex(p >= 0 ? p : 1);
    m_loadingUi = false;

    disconnect(m_profile, nullptr, this, nullptr);
    connect(m_profile, &QComboBox::activated, this, [this](int i) {
        const QString name = m_profile->itemText(i);
        QMessageBox box(QMessageBox::Question, QStringLiteral("Apply profile"),
                        QStringLiteral("Apply the \"%1\" profile?\n\n%2").arg(name, profileDescription(name)),
                        QMessageBox::NoButton, this);
        auto* ok = box.addButton(QStringLiteral("Apply"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != ok) {
            const int cur = m_profile->findText(m_settings.profile);
            m_profile->setCurrentIndex(cur >= 0 ? cur : 1);
            return;
        }
        applyProfile(m_settings, name);
        syncWidgetsFromSettings();
        if (!m_controller->busy())
            m_webcam->apply(m_settings);
        settingsChanged(false);
    });
    updateSummary();
    updateWarnings();
}

void MainWindow::applySettings(const AppSettings& s)
{
    const bool themeChanged = s.theme != m_settings.theme;
    m_settings = s;
    saveSettingsNow();
    if (themeChanged)
        applyTheme(*qApp, m_settings.theme);
    syncWidgetsFromSettings();
    if (!m_controller->busy())
        m_webcam->apply(m_settings);
    else
        m_controller->updateLive();
    registerHotkeys(true);
    applyCaptureExclusion(this);
    if (m_bar->isVisible())
        applyCaptureExclusion(m_bar);
    updateFooter();
}

void MainWindow::updateSummary()
{
    const QSize src = m_controller->sourceSize();
    const QSize out = m_controller->outputSize();
    auto deviceName = [](const QList<AudioEntry>& list, const QString& id) {
        if (id.isEmpty())
            return QStringLiteral("Windows default");
        for (const AudioEntry& e : list)
            if (e.id == id)
                return e.name;
        return QStringLiteral("selected device");
    };
    m_systemName->setText(deviceName(m_scanner->renderDevices(), m_settings.systemDevice));
    m_micName->setText(deviceName(m_scanner->captureDevices(), m_settings.micDevice));

    QStringList audio;
    if (m_settings.systemAudio)
        audio << QStringLiteral("system audio");
    if (m_settings.microphone)
        audio << (m_settings.micMuted ? QStringLiteral("microphone (muted)") : QStringLiteral("microphone"));
    const QString sep = QStringLiteral("  <span style='color:%1'>•</span>  ").arg(currentPalette().border.name());
    QStringList parts;
    parts << m_controller->sourceDescription().toHtmlEscaped();
    if (out.isValid())
        parts << (src != out ? QStringLiteral("%1×%2 → %3×%4").arg(src.width()).arg(src.height()).arg(out.width()).arg(out.height())
                             : QStringLiteral("%1×%2").arg(out.width()).arg(out.height()));
    parts << QStringLiteral("%1 fps").arg(m_settings.fps);
    parts << (audio.isEmpty() ? QStringLiteral("no audio") : audio.join(QStringLiteral(" + ")));
    parts << (m_settings.webcamEnabled ? QStringLiteral("webcam") : QStringLiteral("no webcam"));
    parts << (m_settings.container == Container::Mp4 ? QStringLiteral("MP4") : QStringLiteral("MKV"));
    m_summary->setText(parts.join(sep));

    if (!m_controller->busy()) {
        m_tileOutput->setValue(out.isValid() ? QStringLiteral("%1×%2 • %3").arg(out.width()).arg(out.height()).arg(m_settings.fps)
                                             : QStringLiteral("-"));
        m_tileFps->setValue(QStringLiteral("-"));
        m_tileDropped->setValue(QStringLiteral("-"));
        m_tileSize->setValue(QStringLiteral("-"));
    }
    const QString problem = m_controller->validateSource();
    m_sourceWarning->setText(problem);
    m_sourceWarning->setVisible(!problem.isEmpty() && m_settings.sourceKind != SourceKind::Display);
}

void MainWindow::updateWarnings()
{
    QString w;
    const QSize out = m_controller->outputSize();
    const bool big = out.height() > 900;
    if (m_settings.fps == 60 && big)
        w = QStringLiteral("1080p at 60 fps did not keep up in tests on this PC. 720p60 or 1080p30 are safer.");
    else if (big && m_settings.x264Preset != QStringLiteral("ultrafast"))
        w = QStringLiteral("Slower encoder presets at 1080p dropped frames in tests on this PC; \"ultrafast\" is recommended.");
    else if (m_settings.fps == 60)
        w = QStringLiteral("60 fps needs noticeably more CPU; results depend on content and CPU temperature.");
    m_videoWarning->setText(w);
    m_videoWarning->setVisible(!w.isEmpty());
}

void MainWindow::updateFooter()
{
    m_footerPath->setText(QStringLiteral("Saving to %1").arg(QDir::toNativeSeparators(m_settings.outputDir)));
    const QString dir = m_settings.outputDir;
    QPointer<MainWindow> self(this);
    (void)QtConcurrent::run([self, dir] {
        const int64_t free = freeDiskBytes(dir);
        QMetaObject::invokeMethod(qApp, [self, free] {
            if (self)
                self->m_footerFree->setText(free >= 0 ? QStringLiteral("%1 free").arg(formatBytes(static_cast<uint64_t>(free)))
                                                      : QString());
        });
    });
}

// ---------------------------------------------------------------- devices

void MainWindow::fillMonitors()
{
    m_display->clear();
    int select = 0;
    for (const MonitorEntry& m : m_scanner->monitors()) {
        m_display->addItem(makeIcon(IconId::Display, currentPalette().text), m.label(), m.deviceName);
        if (m.deviceName == m_settings.displayName || (m_settings.displayName.isEmpty() && m.primary))
            select = m_display->count() - 1;
    }
    if (m_display->count() == 0)
        m_display->addItem(QStringLiteral("No display found"));
    m_display->setCurrentIndex(select);
    if (m_settings.displayName.isEmpty() && m_display->count() > 0)
        m_settings.displayName = m_display->itemData(select).toString();
    updateSummary();
    updateWarnings();
}

void MainWindow::fillWindows(const QList<WindowEntry>& windows)
{
    const quintptr current = m_controller->windowTarget();
    m_window->clear();
    int select = -1;
    for (const WindowEntry& w : windows) {
        const QString text = w.process.isEmpty() ? w.title : QStringLiteral("%1  —  %2").arg(w.title, w.process);
        m_window->addItem(text, QVariant::fromValue<quintptr>(w.hwnd));
        m_window->setItemData(m_window->count() - 1, w.title, Qt::ToolTipRole);
        if (w.hwnd == current || (select < 0 && !current && w.title == m_settings.windowTitle))
            select = m_window->count() - 1;
    }
    if (m_window->count() == 0) {
        m_window->addItem(QStringLiteral("No windows found"));
        return;
    }
    if (select >= 0) {
        m_window->setCurrentIndex(select);
        m_controller->setWindowTarget(m_window->itemData(select).value<quintptr>());
    } else {
        m_window->setCurrentIndex(-1);
        m_window->setPlaceholderText(QStringLiteral("Choose a window..."));
    }
    updateSummary();
}

void MainWindow::selectRegion()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    MonitorEntry mon;
    for (const MonitorEntry& m : m_scanner->monitors())
        if (m.deviceName == m_settings.displayName || (m_settings.displayName.isEmpty() && m.primary))
            mon = m;
    for (QScreen* s : QGuiApplication::screens())
        if (s->name() == mon.deviceName)
            screen = s;
    if (!mon.rect.isValid() && !m_scanner->monitors().isEmpty())
        mon = m_scanner->monitors().first();
    if (!mon.rect.isValid())
        mon.rect = QRect(QPoint(0, 0), screen->size() * screen->devicePixelRatio());

    const double aspects[] = {0, 16.0 / 9, 4.0 / 3, 1.0, 9.0 / 16};
    auto* sel = new RegionSelector(screen, mon.rect, aspects[std::clamp(m_regionAspect->currentIndex(), 0, 4)]);
    connect(sel, &RegionSelector::selected, this, [this](const QRect& r) {
        m_settings.region = r;
        m_loadingUi = true;
        m_regionX->setValue(r.x());
        m_regionY->setValue(r.y());
        m_regionW->setValue(r.width());
        m_regionH->setValue(r.height());
        m_loadingUi = false;
        settingsChanged(false);
        showNormal();
        activateWindow();
    });
    connect(sel, &RegionSelector::cancelled, this, [this] {
        showNormal();
        activateWindow();
    });
    showMinimized();
    sel->show();
    sel->activateWindow();
    sel->setFocus();
}

// -------------------------------------------------------------- recording

void MainWindow::onRecordClicked()
{
    if (m_controller->capturing())
        m_controller->stop();
    else if (!m_controller->busy())
        startWithPreflight();
    // Starting / Stopping / Finalizing: ignore repeated clicks.
}

void MainWindow::startWithPreflight()
{
    const QString problem = m_controller->validateSource();
    if (!problem.isEmpty()) {
        showError(QStringLiteral("Cannot record"), problem);
        return;
    }
    const int64_t free = freeDiskBytes(m_settings.outputDir);
    if (free >= 0 && free < static_cast<int64_t>(m_settings.lowSpaceWarnMB) * 1024 * 1024 &&
        m_automation.selfTestSeconds <= 0) {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Low disk space"),
                        QStringLiteral("Only %1 is free on the recording drive. Record anyway?")
                            .arg(formatBytes(static_cast<uint64_t>(free))),
                        QMessageBox::NoButton, this);
        auto* go = box.addButton(QStringLiteral("Record anyway"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != go)
            return;
    }
    if (m_audioTestButton->isChecked())
        m_audioTestButton->setChecked(false); // free the devices for the recording

    auto begin = [this] {
        if (m_settings.minimizeOnRecord)
            showMinimized();
        m_controller->start();
    };
    if (m_settings.countdownSeconds > 0) {
        QScreen* screen = QGuiApplication::primaryScreen();
        for (QScreen* s : QGuiApplication::screens())
            if (s->name() == m_settings.displayName)
                screen = s;
        auto* cd = new CountdownOverlay(screen, m_settings.countdownSeconds);
        applyCaptureExclusion(cd);
        m_record->setEnabled(false);
        connect(cd, &CountdownOverlay::finished, this, [this, begin] {
            m_record->setEnabled(true);
            begin();
        });
        connect(cd, &CountdownOverlay::cancelled, this, [this] {
            m_record->setEnabled(true);
            notify(0, QStringLiteral("Countdown cancelled"), 4000);
        });
        cd->show();
        cd->activateWindow();
        cd->setFocus();
    } else {
        begin();
    }
}

void MainWindow::onStateChanged(session::RecState s)
{
    using S = session::RecState;
    // The session reads the camera's frames from Starting until the file is finalised.
    m_webcam->setLocked(session::isBusy(s));
    if (s == S::Recording && !m_bar->isVisible()) {
        m_lastFrames = 0;
        m_fpsClock.restart();
        m_measuredFps = 0;
        if (m_settings.showRecordingBar) {
            const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
            m_bar->setMicMuted(m_settings.micMuted, m_settings.microphone);
            m_bar->adjustSize();
            m_bar->move(avail.center().x() - m_bar->width() / 2, avail.top() + 12);
            m_bar->show();
            applyCaptureExclusion(m_bar);
        }
    }
    if (s == S::Stopping || s == S::Finalizing) {
        m_bar->hide();
        m_finishTicker.start();
    } else {
        m_finishTicker.stop();
    }
    if (s == S::Idle || s == S::Error) {
        m_bar->hide();
        m_timer->setText(QStringLiteral("00:00:00"));
        m_systemMeter->setLevel(0);
        m_micMeter->setLevel(0);
        m_webcam->apply(m_settings); // apply camera changes made during the recording
        updateSummary();
        updateFooter();
        if (m_quitAfterStop)
            QTimer::singleShot(0, this, &QWidget::close);
    }
    updateRecordingUi();
}

void MainWindow::updateRecordingUi()
{
    using S = session::RecState;
    const auto st = m_controller->state();
    const bool capturing = session::isCapturing(st);
    const bool busy = session::isBusy(st);

    m_record->setText(capturing ? QStringLiteral("  Stop recording") : QStringLiteral("  Start recording"));
    m_record->setIcon(makeIcon(capturing ? IconId::Stop : IconId::Record, capturing ? currentPalette().record : QColor(Qt::white)));
    m_record->setProperty("recording", capturing);
    m_record->style()->unpolish(m_record);
    m_record->style()->polish(m_record);
    m_record->setEnabled(!busy || capturing);
    m_pause->setEnabled(capturing);
    m_pause->setIcon(makeIcon(st == S::Paused ? IconId::Resume : IconId::Pause, currentPalette().text));
    m_sourceCard->setEnabled(!busy);
    m_videoCard->setEnabled(!busy);
    m_systemAudio->setEnabled(!busy);
    m_mic->setEnabled(!busy);
    m_audioTestButton->setEnabled(!busy);
    m_profile->setEnabled(!busy);
    if (m_trayRecord)
        m_trayRecord->setText(capturing ? QStringLiteral("Stop recording") : QStringLiteral("Start recording"));
    if (m_tray)
        m_tray->setToolTip(capturing ? QStringLiteral("LumaCapture - recording") : QStringLiteral("LumaCapture"));

    const char* pillState = "idle";
    QString pillText = QStringLiteral("READY");
    switch (st) {
    case S::Idle: m_stateText->setText(QStringLiteral("Ready to record")); break;
    case S::Error:
        pillState = "error";
        pillText = QStringLiteral("ERROR");
        m_stateText->setText(QStringLiteral("The last recording had a problem - see the message. Ready to record again."));
        break;
    case S::Starting:
        pillState = "busy";
        pillText = QStringLiteral("STARTING");
        m_stateText->setText(QStringLiteral("Starting capture, encoder and devices..."));
        break;
    case S::Recording:
        pillState = "rec";
        pillText = QStringLiteral("● REC");
        break;
    case S::Paused:
        pillState = "paused";
        pillText = QStringLiteral("PAUSED");
        m_stateText->setText(QStringLiteral("Paused - the paused time is not included in the video"));
        break;
    case S::Stopping:
    case S::Finalizing:
        pillState = "busy";
        pillText = QStringLiteral("SAVING");
        break;
    }
    m_pill->setText(pillText);
    m_pill->setProperty("state", pillState);
    m_pill->style()->unpolish(m_pill);
    m_pill->style()->polish(m_pill);
}

void MainWindow::onStatusTick()
{
    const session::SessionStatus& st = m_controller->lastStatus();
    const QString time = formatDuration(st.elapsedSeconds);
    m_timer->setText(time);
    if (m_fpsClock.elapsed() >= 1000) {
        m_measuredFps = (st.capturedFrames - m_lastFrames) * 1000.0 / m_fpsClock.elapsed();
        m_lastFrames = st.capturedFrames;
        m_fpsClock.restart();
    }
    const QSize out = m_controller->outputSize();
    m_tileOutput->setValue(QStringLiteral("%1×%2 • %3").arg(out.width()).arg(out.height()).arg(m_settings.fps));
    m_tileFps->setValue(st.paused ? QStringLiteral("paused") : QStringLiteral("%1 fps").arg(m_measuredFps, 0, 'f', 0),
                        !st.paused && m_measuredFps > 0 && m_measuredFps < m_settings.fps * 0.9);
    m_tileDropped->setValue(QString::number(st.droppedFrames), st.droppedFrames > 0);
    m_tileSize->setValue(formatBytes(st.bytes));
    if (!st.paused)
        m_stateText->setText(QStringLiteral("Recording to %1").arg(QFileInfo(m_controller->currentFile()).fileName()));
    m_systemMeter->setLevel(st.systemPeak);
    m_micMeter->setLevel(st.micPeak);
    m_bar->setElapsed(time, st.paused);
    QString warn = QString::fromStdString(st.sourceStatus);
    if (!st.audioStatus.empty())
        warn += (warn.isEmpty() ? QString() : QStringLiteral("\n")) + QString::fromStdString(st.audioStatus);
    m_sourceWarning->setText(warn);
    m_sourceWarning->setVisible(!warn.isEmpty());
}

void MainWindow::onHotkey(HotkeyAction a)
{
    switch (a) {
    case HotkeyAction::RecordToggle: onRecordClicked(); break;
    case HotkeyAction::Stop: m_controller->stop(); break;
    case HotkeyAction::PauseResume: m_controller->togglePause(); break;
    case HotkeyAction::MicMute:
        if (m_settings.microphone)
            m_micMute->toggle();
        break;
    case HotkeyAction::Screenshot: m_controller->takeScreenshot(); break;
    default: break;
    }
}

// ------------------------------------------------------------------ dialogs

void MainWindow::openSettings(int page)
{
    if (m_settingsDialog) {
        m_settingsDialog->showPage(static_cast<SettingsDialog::Page>(page));
        m_settingsDialog->raise();
        m_settingsDialog->activateWindow();
        return;
    }
    auto* dlg = new SettingsDialog(m_settings, *m_scanner, *m_webcam, m_controller->busy(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_settingsDialog = dlg;
    connect(dlg, &SettingsDialog::applied, this, &MainWindow::applySettings);
    connect(dlg, &SettingsDialog::recoveryScanRequested, m_controller, &RecordingController::scanForRecovery);
    dlg->showPage(static_cast<SettingsDialog::Page>(page));
    dlg->show();
    applyCaptureExclusion(dlg);
}

void MainWindow::openLayout()
{
    if (m_layoutDialog) {
        m_layoutDialog->raise();
        m_layoutDialog->activateWindow();
        return;
    }
    // Background: a small grab of the selected display (for orientation only).
    QPixmap background;
    if (m_settings.sourceKind == SourceKind::Display && m_automation.snapshotDir.isEmpty()) {
        for (QScreen* s : QGuiApplication::screens())
            if (s->name() == m_settings.displayName || (m_settings.displayName.isEmpty() && s == QGuiApplication::primaryScreen()))
                background = s->grabWindow(0).scaledToWidth(960, Qt::SmoothTransformation);
    }
    auto* dlg = new LayoutDialog(m_settings, *m_webcam, m_controller->outputSize(), background, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_layoutDialog = dlg;
    connect(dlg, &LayoutDialog::changed, this, [this] {
        m_loadingUi = true;
        m_webcamOn->setChecked(m_settings.webcamEnabled);
        m_webcamPreview->setMirror(m_settings.webcamPlacement.mirror);
        m_loadingUi = false;
        if (!m_controller->busy())
            m_webcam->apply(m_settings);
        settingsChanged();
    });
    dlg->show();
    applyCaptureExclusion(dlg);
}

void MainWindow::refreshRecent()
{
    m_recent->clear();
    const auto& entries = m_history.entries();
    if (entries.isEmpty()) {
        auto* it = new QListWidgetItem(QStringLiteral("No recordings yet"));
        it->setData(kRoleMeta, QStringLiteral("Press Start recording - your files will be listed here."));
        it->setData(kRoleMissing, true);
        it->setFlags(Qt::NoItemFlags);
        m_recent->addItem(it);
        return;
    }
    for (const RecordingEntry& e : entries) {
        QStringList meta;
        meta << e.created.toString(QStringLiteral("d MMM yyyy, HH:mm"));
        if (e.durationSeconds > 0)
            meta << formatDuration(e.durationSeconds);
        if (e.width > 0)
            meta << QStringLiteral("%1×%2 @ %3 fps").arg(e.width).arg(e.height).arg(e.fps);
        meta << formatBytes(static_cast<uint64_t>(e.sizeBytes));
        meta << (e.exists ? QDir::toNativeSeparators(QFileInfo(e.path).path()) : QStringLiteral("file missing"));
        auto* it = new QListWidgetItem(QFileInfo(e.path).fileName());
        it->setData(kRolePath, e.path);
        it->setData(kRoleMeta, meta.join(QStringLiteral("  •  ")));
        it->setData(kRoleMissing, !e.exists);
        it->setToolTip(QDir::toNativeSeparators(e.path));
        m_recent->addItem(it);
    }
}

void MainWindow::showError(const QString& title, const QString& message)
{
    log::warn("{}: {}", title.toStdString(), message.toStdString());
    notify(3, QStringLiteral("<b>%1</b> - %2").arg(title.toHtmlEscaped(), message.toHtmlEscaped().replace('\n', QStringLiteral("<br>"))), 0);
}

// -------------------------------------------------------------- automation

void MainWindow::runSelfTest()
{
    // Scripted GUI check (used by the test scripts): record, pause, resume, stop,
    // repeated/invalid requests, webcam on/off, settings dialog, then quit.
    // Writes only into the output directory given on the command line.
    auto logStep = [](const QString& s) { log::info("SELFTEST: {}", s.toStdString()); };
    auto fail = [logStep](const QString& why) {
        logStep(QStringLiteral("FAIL - ") + why);
        QApplication::exit(1);
    };
    QTimer::singleShot(180000, this, [fail] { fail(QStringLiteral("overall timeout (180 s)")); });

    const int half = std::max(1, m_automation.selfTestSeconds / 2) * 1000;
    logStep(QStringLiteral("start recording"));
    m_controller->start();
    m_controller->start(); // must be ignored (already starting)

    auto* waitRec = new QTimer(this);
    waitRec->setInterval(100);
    auto phase = std::make_shared<int>(0);
    auto phaseClock = std::make_shared<QElapsedTimer>();
    phaseClock->start();
    connect(waitRec, &QTimer::timeout, this, [this, phase, phaseClock, half, logStep, fail, waitRec] {
        const auto st = m_controller->state();
        const qint64 t = phaseClock->elapsed();
        auto next = [&] {
            ++*phase;
            phaseClock->restart();
        };
        switch (*phase) {
        case 0: // wait for Recording
            if (st == session::RecState::Recording) {
                logStep(QStringLiteral("recording"));
                next();
            } else if (st == session::RecState::Error || t > 15000) {
                waitRec->stop();
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
        case 4: // wait for Idle
            if (st == session::RecState::Idle) {
                const QString f = m_controller->lastFile();
                logStep(QStringLiteral("saved %1 (%2 bytes)").arg(f).arg(QFileInfo(f).size()));
                if (!QFileInfo::exists(f)) {
                    waitRec->stop();
                    fail(QStringLiteral("no file saved"));
                    return;
                }
                next();
            } else if (st == session::RecState::Error || t > 60000) {
                waitRec->stop();
                fail(QStringLiteral("stop did not finish"));
            }
            break;
        case 5:
            if (m_scanner->cameras().isEmpty()) {
                logStep(QStringLiteral("no camera - skipping webcam steps"));
                *phase = 9;
                phaseClock->restart();
                break;
            }
            logStep(QStringLiteral("webcam on"));
            m_webcamOn->setChecked(true);
            next();
            break;
        case 6:
            if (t > 3000) {
                logStep(QStringLiteral("webcam off (previously hung the UI)"));
                m_webcamOn->setChecked(false);
                next();
            }
            break;
        case 7:
            if (t > 1500) {
                logStep(QStringLiteral("open webcam settings page"));
                openSettings(SettingsDialog::Webcam);
                next();
            }
            break;
        case 8:
            if (t > 4000) {
                logStep(QStringLiteral("close settings"));
                if (m_settingsDialog)
                    m_settingsDialog->reject();
                next();
            }
            break;
        case 9:
            if (t > 1500) {
                waitRec->stop();
                logStep(QStringLiteral("PASS"));
                QApplication::exit(0);
            }
            break;
        }
    });
    waitRec->start();
}

void MainWindow::runSnapshots()
{
    // Renders LumaCapture's own windows to PNG (QWidget::grab - no screen capture).
    const QString dir = m_automation.snapshotDir;
    QDir().mkpath(dir);
    grab().save(dir + QStringLiteral("/main.png"));
    openSettings(SettingsDialog::General);
    for (int page = 0; page <= SettingsDialog::About; ++page) {
        m_settingsDialog->showPage(static_cast<SettingsDialog::Page>(page));
        QApplication::processEvents();
        m_settingsDialog->grab().save(dir + QStringLiteral("/settings-%1.png").arg(page));
    }
    m_settingsDialog->reject();
    openLayout();
    QApplication::processEvents();
    if (m_layoutDialog)
        m_layoutDialog->grab().save(dir + QStringLiteral("/layout.png"));
    log::info("UI snapshots written to {}", dir.toStdString());
    QTimer::singleShot(500, this, [] { QApplication::exit(0); });
}

// ------------------------------------------------------------------- events

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_controller->busy()) {
        if (!m_quitAfterStop) {
            QMessageBox box(QMessageBox::Question, QStringLiteral("Recording in progress"),
                            QStringLiteral("A recording is running. Stop it, save the file and quit?"),
                            QMessageBox::NoButton, this);
            auto* yes = box.addButton(QStringLiteral("Stop, save and quit"), QMessageBox::AcceptRole);
            box.addButton(QMessageBox::Cancel);
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
    saveSettingsNow();
    m_hotkeys->unregisterAll();
    m_audioTest->stop();
    m_webcam->stopCamera();
    if (m_tray)
        m_tray->hide();
    e->accept();
    qApp->quit();
}

void MainWindow::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange && isMinimized() && m_settings.minimizeToTray && m_tray)
        QTimer::singleShot(0, this, &QWidget::hide);
}

} // namespace luma::app
