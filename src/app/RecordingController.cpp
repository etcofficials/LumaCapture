#include "RecordingController.h"

#include "DeviceScanner.h"
#include "WebcamController.h"
#include "WinUtil.h"
#include "capture/Screenshot.h"
#include "mux/Muxer.h"
#include "session/PreviewExchange.h"
#include "util/Log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>

namespace luma::app {
namespace {

// Runs `work` on a worker thread and `done` with its result on the UI thread.
template <typename Work, typename Done>
void runAsync(QObject* ctx, Work work, Done done)
{
    QPointer<QObject> guard(ctx);
    (void)QtConcurrent::run([guard, work = std::move(work), done = std::move(done)]() mutable {
        auto result = work();
        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, done = std::move(done), result]() mutable {
            if (guard)
                done(result);
        });
    });
}

QRect windowBounds(HWND hwnd)
{
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(hwnd, &r);
    return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

} // namespace

RecordingController::RecordingController(AppSettings& settings, WebcamController& webcam, DeviceScanner& scanner,
                                         History& history, QObject* parent)
    : QObject(parent), m_settings(settings), m_webcam(webcam), m_scanner(scanner), m_history(history)
{
    m_statusTimer.setInterval(250);
    connect(&m_statusTimer, &QTimer::timeout, this, &RecordingController::pollStatus);
}

RecordingController::~RecordingController()
{
    m_statusTimer.stop();
    if (m_session) {
        // App is closing during a recording (or a finalisation still runs on a
        // worker): stop() is idempotent and serialised, so this waits for the
        // file to be finalised instead of racing the worker.
        try {
            m_session->stop();
        } catch (const std::exception& e) {
            log::error("Finalising on exit failed: {}", e.what());
        }
    }
}

bool RecordingController::apply(session::RecEvent e)
{
    const State next = session::nextState(m_state, e);
    if (next == m_state)
        return false; // not allowed in this state (e.g. a second Stop) - ignore
    log::info("Recording state: {} -> {}", session::stateName(m_state), session::stateName(next));
    m_state = next;
    if (next == State::Stopping)
        m_finishClock.start();
    else if (next == State::Idle || next == State::Error)
        m_finishClock.invalidate();
    emit stateChanged(next);
    return true;
}

int RecordingController::monitorIndex(QRect* rect) const
{
    const auto& mons = m_scanner.monitors();
    int best = -1;
    for (const MonitorEntry& m : mons) {
        if (!m_settings.displayName.isEmpty() && m.deviceName == m_settings.displayName) {
            if (rect)
                *rect = m.rect;
            return m.index;
        }
    }
    for (const MonitorEntry& m : mons) {
        if (m.primary || best < 0) {
            best = m.index;
            if (rect)
                *rect = m.rect;
            if (m.primary)
                break;
        }
    }
    return best;
}

int RecordingController::monitorOfWindow(quintptr hwnd, QRect* rect) const
{
    const HWND w = reinterpret_cast<HWND>(hwnd);
    if (!w || !IsWindow(w))
        return -1;
    HMONITOR mon = MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi))
        return -1;
    const QString device = QString::fromWCharArray(mi.szDevice);
    for (const MonitorEntry& m : m_scanner.monitors()) {
        if (m.deviceName.compare(device, Qt::CaseInsensitive) == 0) {
            if (rect)
                *rect = m.rect;
            return m.index;
        }
    }
    return -1;
}

QSize RecordingController::sourceSize() const
{
    QSize size;
    switch (m_settings.sourceKind) {
    case SourceKind::Display: {
        QRect r;
        if (monitorIndex(&r) >= 0)
            size = r.size();
        break;
    }
    case SourceKind::Window: {
        const HWND hwnd = reinterpret_cast<HWND>(m_window);
        if (hwnd && IsWindow(hwnd))
            size = windowBounds(hwnd).size();
        break;
    }
    case SourceKind::Region:
        size = m_settings.region.size();
        break;
    case SourceKind::Game: {
        QRect r;
        if (monitorOfWindow(m_game, &r) >= 0)
            size = r.size();
        break;
    }
    }
    if ((m_settings.sourceKind == SourceKind::Display || m_settings.sourceKind == SourceKind::Window) &&
        size.isValid()) {
        const int w = size.width() - m_settings.cropLeft - m_settings.cropRight;
        const int h = size.height() - m_settings.cropTop - m_settings.cropBottom;
        if (w > 16 && h > 16)
            size = QSize(w, h);
    }
    return size;
}

QSize RecordingController::outputSize() const
{
    return scaledOutputSize(sourceSize(), m_settings.resolution);
}

QString RecordingController::sourceDescription() const
{
    switch (m_settings.sourceKind) {
    case SourceKind::Display: {
        const int idx = monitorIndex(nullptr);
        for (const MonitorEntry& m : m_scanner.monitors())
            if (m.index == idx)
                return QStringLiteral("Display %1%2").arg(m.index + 1).arg(m.primary ? QStringLiteral(" (primary)") : QString());
        return QStringLiteral("Display");
    }
    case SourceKind::Window:
        return m_settings.windowTitle.isEmpty() ? QStringLiteral("Window") : QStringLiteral("Window \"%1\"").arg(m_settings.windowTitle);
    case SourceKind::Region:
        return QStringLiteral("Region %1×%2").arg(m_settings.region.width()).arg(m_settings.region.height());
    case SourceKind::Game:
        return m_settings.gameTitle.isEmpty() ? QStringLiteral("Game") : QStringLiteral("Game \"%1\"").arg(m_settings.gameTitle);
    }
    return {};
}

QString RecordingController::validateSource() const
{
    switch (m_settings.sourceKind) {
    case SourceKind::Display:
        if (m_scanner.monitors().isEmpty())
            return QStringLiteral("No display found yet. Wait a moment or press Refresh.");
        break;
    case SourceKind::Window: {
        const HWND hwnd = reinterpret_cast<HWND>(m_window);
        if (!hwnd || !IsWindow(hwnd))
            return QStringLiteral("Choose a window to record (the previously selected window is gone).");
        if (IsIconic(hwnd))
            return QStringLiteral("The selected window is minimized. Restore it first.");
        break;
    }
    case SourceKind::Region:
        if (m_settings.region.width() < 32 || m_settings.region.height() < 32)
            return QStringLiteral("Select a region of at least 32×32 pixels.");
        break;
    case SourceKind::Game: {
        const HWND hwnd = reinterpret_cast<HWND>(m_game);
        if (!hwnd || !IsWindow(hwnd))
            return QStringLiteral("Start your game, then choose its window under Game.");
        if (monitorOfWindow(m_game, nullptr) < 0)
            return QStringLiteral("The game's monitor was not found. Press Refresh.");
        break;
    }
    }
    return {};
}

QRect RecordingController::capturedScreenRect() const
{
    QRect r;
    switch (m_settings.sourceKind) {
    case SourceKind::Display:
        monitorIndex(&r);
        if (m_settings.cropLeft || m_settings.cropTop || m_settings.cropRight || m_settings.cropBottom)
            r.adjust(m_settings.cropLeft, m_settings.cropTop, -m_settings.cropRight, -m_settings.cropBottom);
        break;
    case SourceKind::Region:
        r = m_settings.region;
        break;
    case SourceKind::Game:
        monitorOfWindow(m_game, &r);
        break;
    case SourceKind::Window:
        break;
    }
    return r;
}

QString RecordingController::makeOutputPath() const
{
    const QDateTime now = QDateTime::currentDateTime();
    const QSize out = outputSize();
    QString name = m_settings.namePattern.isEmpty() ? QStringLiteral("LumaCapture_{date}_{time}") : m_settings.namePattern;
    name.replace(QStringLiteral("{date}"), now.toString(QStringLiteral("yyyy-MM-dd")));
    name.replace(QStringLiteral("{time}"), now.toString(QStringLiteral("HH-mm-ss")));
    name.replace(QStringLiteral("{res}"), QStringLiteral("%1x%2").arg(out.width()).arg(out.height()));
    name.replace(QStringLiteral("{fps}"), QString::number(m_settings.fps));
    name.replace(QStringLiteral("{source}"), sourceKindName(m_settings.sourceKind));
    name = sanitizeFileName(name);

    const QDir dir(m_settings.outputDir);
    QString path = dir.filePath(name + QStringLiteral(".mkv"));
    for (int i = 2; QFileInfo::exists(path) || QFileInfo::exists(dir.filePath(name + QStringLiteral(".mp4"))); ++i)
        path = dir.filePath(QStringLiteral("%1_%2.mkv").arg(name).arg(i));
    return path;
}

gpu::CompositionSettings RecordingController::buildComposition(int outW, int outH, bool withWebcam)
{
    gpu::CompositionSettings c;
    c.filters = m_settings.videoFilters;
    c.cursor = m_settings.cursor;
    if (!m_settings.captureCursor) {
        c.cursor.highlight = false;
        c.cursor.clicks = false;
    }
    c.webcam = resolvedPlacement(m_settings.webcamPlacement, m_webcam.aspect(m_settings.webcamPlacement), outW, outH);
    c.webcam.enabled = withWebcam && m_settings.webcamEnabled && m_webcam.frames() != nullptr;
    c.overlay = m_overlay.render(m_settings, outW, outH);
    return c;
}

bool RecordingController::fillSource(session::SourceConfig& src, QString& error) const
{
    error = validateSource();
    if (!error.isEmpty())
        return false;
    src.captureCursor = m_settings.captureCursor;
    switch (m_settings.sourceKind) {
    case SourceKind::Display:
        src.kind = session::SourceKind::Display;
        src.outputIndex = static_cast<unsigned>(std::max(0, monitorIndex(nullptr)));
        break;
    case SourceKind::Window:
        src.kind = session::SourceKind::Window;
        src.window = reinterpret_cast<HWND>(m_window);
        break;
    case SourceKind::Region: {
        src.kind = session::SourceKind::Region;
        const QRect r = m_settings.region;
        src.region = RECT{r.left(), r.top(), r.left() + r.width(), r.top() + r.height()};
        break;
    }
    case SourceKind::Game:
        // The game's whole monitor through Desktop Duplication: works for full-screen and
        // borderless games, follows exclusive-mode switches, and shows no capture border.
        src.kind = session::SourceKind::Display;
        src.outputIndex = static_cast<unsigned>(std::max(0, monitorOfWindow(m_game, nullptr)));
        break;
    }
    if ((m_settings.sourceKind == SourceKind::Display || m_settings.sourceKind == SourceKind::Window) &&
        (m_settings.cropLeft || m_settings.cropTop || m_settings.cropRight || m_settings.cropBottom)) {
        QSize full = sourceSize();
        full = QSize(full.width() + m_settings.cropLeft + m_settings.cropRight,
                     full.height() + m_settings.cropTop + m_settings.cropBottom);
        src.crop = RECT{m_settings.cropLeft, m_settings.cropTop, full.width() - m_settings.cropRight,
                        full.height() - m_settings.cropBottom};
    }
    return true;
}

session::SessionConfig RecordingController::buildConfig(QString& error)
{
    session::SessionConfig cfg;
    if (!fillSource(cfg.source, error))
        return cfg;

    const QSize out = outputSize();
    if (out.isEmpty()) {
        error = QStringLiteral("Could not determine the size of the capture source.");
        return cfg;
    }
    m_outSize = out;
    cfg.width = out.width();
    cfg.height = out.height();
    cfg.fps = m_settings.fps;
    cfg.cpuConvert = m_settings.cpuConvert;
    cfg.queueCapacity = static_cast<unsigned>(m_settings.queueCapacity);
    const EncoderChoice enc = resolveEncoder(m_settings);
    cfg.encoder.preset = enc.preset.toStdString();
    cfg.encoder.crf = enc.crf;
    cfg.encoder.x264Params = enc.x264Params.toStdString();
    cfg.encoder.keyframeSeconds = m_settings.keyframeSeconds;
    cfg.encoder.threads = m_settings.encoderThreads;

    cfg.recordAudio = m_settings.systemAudio || m_settings.microphone;
    auto& a = cfg.audio;
    a.system = m_settings.systemAudio;
    a.mic = m_settings.microphone;
    a.systemDevice = m_settings.systemDevice.toStdWString();
    a.micDevice = m_settings.micDevice.toStdWString();
    a.systemVolume = static_cast<float>(m_settings.systemVolume);
    a.micVolume = static_cast<float>(m_settings.micVolume);
    a.micMuted = m_settings.micMuted;
    a.micFilters = m_settings.micFilters;
    a.micDelayMs = m_settings.micDelayMs;
    a.bitrateKbps = m_settings.audioBitrate;
    a.separateTracks = m_settings.separateTracks;

    if (m_settings.webcamEnabled)
        cfg.webcamFrames = m_webcam.frames();
    cfg.composition = buildComposition(out.width(), out.height(), true);
    if (m_preview && m_settings.livePreview && m_settings.previewWhileRecording) {
        cfg.preview = m_preview;
        cfg.previewIntervalMs = static_cast<unsigned>(1000 / std::clamp(m_settings.previewFps, 2, 15));
    }
    cfg.outputFile = makeOutputPath().toStdWString();
    return cfg;
}

session::SessionConfig RecordingController::buildPreviewConfig(QString& error)
{
    session::SessionConfig cfg;
    if (!fillSource(cfg.source, error))
        return cfg;
    QSize out = outputSize();
    if (out.isEmpty()) {
        error = QStringLiteral("Could not determine the size of the capture source.");
        return cfg;
    }
    // The idle preview never needs more than ~540p: compose at a reduced size (cheaper on
    // the GPU); overlays and the webcam placement scale with the frame.
    if (out.height() > 540)
        out = QSize(static_cast<int>(std::lround(out.width() * 540.0 / out.height())) & ~1, 540);
    const int fps = std::clamp(m_settings.previewFps, 2, 15);
    cfg.width = out.width();
    cfg.height = out.height();
    cfg.fps = fps;
    cfg.previewOnly = true;
    cfg.preview = m_preview;
    cfg.previewIntervalMs = static_cast<unsigned>(1000 / fps);
    cfg.recordAudio = false;
    // No webcam in the idle preview: it would tie the camera to the preview session.
    cfg.composition = buildComposition(out.width(), out.height(), false);
    return cfg;
}

void RecordingController::start()
{
    if (busy()) {
        log::info("Start ignored: recording state is {}", session::stateName(m_state));
        return;
    }
    QString error;
    session::SessionConfig cfg = buildConfig(error);
    if (!error.isEmpty()) {
        emit errorOccurred(QStringLiteral("Cannot record"), error);
        return;
    }
    if (!apply(session::RecEvent::StartRequested))
        return;
    m_currentFile = QString::fromStdWString(cfg.outputFile.wstring());
    m_stopRequestedByError = false;
    m_lastStatus = {};
    m_dropWindow.clear();
    m_lastBehindWarning.invalidate();
    const QString outputDir = m_settings.outputDir;

    std::shared_ptr<session::RecordingSession> sessionPtr;
    try {
        sessionPtr = std::make_shared<session::RecordingSession>(std::move(cfg));
    } catch (const std::exception& e) {
        apply(session::RecEvent::StartFailed);
        emit errorOccurred(QStringLiteral("Recording could not start"), QString::fromUtf8(e.what()));
        return;
    }
    runAsync(
        this,
        [sessionPtr, outputDir]() -> QString {
            // The output folder is created here, not on the UI thread: on a sleeping or
            // slow HDD even this can take seconds.
            if (!QDir().mkpath(outputDir))
                return QStringLiteral("The output folder %1 cannot be created. Choose another folder in "
                                      "Settings > Output.")
                    .arg(QDir::toNativeSeparators(outputDir));
            try {
                sessionPtr->start();
                return {};
            } catch (const std::exception& e) {
                return QString::fromUtf8(e.what());
            } catch (...) {
                return QStringLiteral("unexpected error");
            }
        },
        [this, sessionPtr](const QString& err) {
            if (!err.isEmpty()) {
                log::error("Recording could not start: {}", err.toStdString());
                m_currentFile.clear();
                apply(session::RecEvent::StartFailed);
                emit errorOccurred(QStringLiteral("Recording could not start"),
                                   QStringLiteral("%1\n\nNothing was recorded. Check that the selected screen, "
                                                  "window, camera and audio devices are available.")
                                       .arg(err));
                return;
            }
            m_session = sessionPtr;
            m_diskCheckCounter = 0;
            m_statusTimer.start();
            apply(session::RecEvent::StartSucceeded);
            emit info(QStringLiteral("Recording to %1").arg(QDir::toNativeSeparators(m_currentFile)));
        });
}

void RecordingController::pollStatus()
{
    if (!m_session || !capturing())
        return;
    m_lastStatus = m_session->status();
    emit statusTick();

    if (m_lastStatus.failed && !m_stopRequestedByError) {
        m_stopRequestedByError = true;
        emit errorOccurred(QStringLiteral("Recording stopped"),
                           QStringLiteral("%1\n\nThe part recorded until the problem is being saved.")
                               .arg(QString::fromStdString(m_lastStatus.error)));
        stop();
        return;
    }

    // Falling behind: more than 2 % of the frames of the last ~5 s were dropped.
    m_dropWindow.emplace_back(m_lastStatus.ticks, m_lastStatus.droppedFrames);
    while (m_dropWindow.size() > 20)
        m_dropWindow.pop_front();
    if (m_settings.warnFallingBehind && !m_lastStatus.paused && m_dropWindow.size() >= 12) {
        const int64_t ticks = m_lastStatus.ticks - m_dropWindow.front().first;
        const int64_t drops = m_lastStatus.droppedFrames - m_dropWindow.front().second;
        if (ticks > 0 && drops >= 5 && drops * 100 > ticks * 2 &&
            (!m_lastBehindWarning.isValid() || m_lastBehindWarning.elapsed() > 30000)) {
            m_lastBehindWarning.start();
            log::warn("Recording is falling behind: {} of {} frames dropped in the last seconds", drops, ticks);
            emit fallingBehind(100.0 * static_cast<double>(drops) / static_cast<double>(ticks));
        }
    }

    if (++m_diskCheckCounter >= 20) { // every 5 s, off the UI thread
        m_diskCheckCounter = 0;
        const QString dir = m_settings.outputDir;
        runAsync(
            this, [dir]() -> qint64 { return freeDiskBytes(dir); },
            [this](qint64 free) {
                if (free >= 0 && free < 300ll * 1024 * 1024 && capturing() && !m_stopRequestedByError) {
                    m_stopRequestedByError = true;
                    emit errorOccurred(QStringLiteral("Disk almost full"),
                                       QStringLiteral("Less than 300 MB is left on the recording drive. The "
                                                      "recording was stopped and saved to avoid a damaged file."));
                    stop();
                }
            });
    }
}

void RecordingController::takeAudioPeaks(float& system, float& mic)
{
    system = mic = 0.f;
    if (m_session && capturing())
        m_session->takeAudioPeaks(system, mic);
}

void RecordingController::stop()
{
    if (!m_session || !apply(session::RecEvent::StopRequested)) {
        log::info("Stop ignored: recording state is {}", session::stateName(m_state));
        return;
    }
    m_statusTimer.stop();
    emit info(QStringLiteral("Finishing the recording..."));

    struct StopResult {
        QString file;
        QString error;
        QString remuxError;
        mux::MediaInfo media;
        bool probed = false;
        bool fileOk = false;
        qint64 size = 0;
    };
    auto sessionPtr = m_session;
    const bool toMp4 = m_settings.container == Container::Mp4;
    const bool keepMkv = m_settings.keepMkvAfterMp4;
    const QString mkv = m_currentFile;
    QPointer<RecordingController> self(this);
    runAsync(
        this,
        [sessionPtr, toMp4, keepMkv, mkv, self]() -> StopResult {
            StopResult r;
            r.file = mkv;
            try {
                sessionPtr->stop(); // flushes encoders, drains the file writer, writes the trailer
            } catch (const std::exception& e) {
                r.error = QString::fromUtf8(e.what());
            } catch (...) {
                r.error = QStringLiteral("unexpected error while finishing the file");
            }
            if (sessionPtr->failed())
                r.error = QString::fromStdString(sessionPtr->errorMessage());
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self] {
                if (self && self->m_state == State::Stopping)
                    self->apply(session::RecEvent::SessionStopped);
            });
            if (toMp4 && QFileInfo::exists(mkv)) {
                const QString mp4 = QFileInfo(mkv).path() + '/' + QFileInfo(mkv).completeBaseName() + ".mp4";
                try {
                    mux::remux(mkv.toStdWString(), mp4.toStdWString());
                    r.file = mp4;
                    if (!keepMkv)
                        QFile::remove(mkv);
                } catch (const std::exception& e) {
                    r.remuxError = QString::fromUtf8(e.what());
                    QFile::remove(mp4);
                }
            }
            r.probed = mux::probeMedia(r.file.toStdWString(), r.media);
            r.fileOk = r.probed && QFileInfo::exists(r.file);
            r.size = QFileInfo(r.file).size();
            return r;
        },
        [this](const StopResult& r) {
            m_session.reset();
            m_currentFile.clear();
            if (m_state == State::Stopping)
                apply(session::RecEvent::SessionStopped); // queued notification not processed yet
            if (r.fileOk) {
                m_lastFile = r.file;
                RecordingEntry e;
                e.path = r.file;
                e.created = QDateTime::currentDateTime();
                e.durationSeconds = r.media.durationSeconds;
                e.width = r.media.width;
                e.height = r.media.height;
                e.fps = r.media.fps > 0 ? r.media.fps : m_settings.fps;
                e.sizeBytes = r.size;
                m_history.add(e);
                emit historyChanged();
                emit recordingSaved(r.file, !r.error.isEmpty());
            }
            if (!r.remuxError.isEmpty())
                emit errorOccurred(QStringLiteral("MP4 conversion failed"),
                                   QStringLiteral("The MKV recording was kept and is playable.\n\n%1").arg(r.remuxError));
            if (!r.fileOk) {
                apply(session::RecEvent::FinalizeFailed);
                emit errorOccurred(QStringLiteral("Recording not saved"),
                                   QStringLiteral("The recording file could not be finalised or read back.%1")
                                       .arg(r.error.isEmpty() ? QString() : QStringLiteral("\n\n") + r.error));
                return;
            }
            if (!r.error.isEmpty() && !m_stopRequestedByError)
                emit errorOccurred(QStringLiteral("Recording finished with problems"),
                                   QStringLiteral("The file was saved, but: %1").arg(r.error));
            apply(session::RecEvent::FinalizeDone);
        });
}

void RecordingController::togglePause()
{
    if (!m_session)
        return;
    if (m_state == State::Recording && apply(session::RecEvent::PauseRequested))
        m_session->pause();
    else if (m_state == State::Paused && apply(session::RecEvent::ResumeRequested))
        m_session->resume();
    else
        return;
    m_lastStatus = m_session->status();
    emit statusTick();
}

void RecordingController::setMicMuted(bool muted)
{
    m_settings.micMuted = muted;
    if (m_session)
        m_session->setMicMuted(muted);
}

void RecordingController::updateLive()
{
    if (!m_session || !capturing())
        return;
    m_session->setComposition(buildComposition(m_outSize.width(), m_outSize.height(), true));
    m_session->setAudioVolumes(static_cast<float>(m_settings.systemVolume), static_cast<float>(m_settings.micVolume));
    m_session->setMicFilters(m_settings.micFilters);
}

void RecordingController::takeScreenshot()
{
    const QString dir = m_settings.screenshotDir.isEmpty() ? m_settings.outputDir + QStringLiteral("/Screenshots")
                                                           : m_settings.screenshotDir;
    const QString file = QDir(dir).filePath(
        QStringLiteral("Screenshot_%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz"))));
    const bool cursor = m_settings.screenshotCursor && m_settings.captureCursor;
    const SourceKind kind = m_settings.sourceKind;
    const HWND hwnd = reinterpret_cast<HWND>(m_window);
    const QString problem = validateSource();
    if (!problem.isEmpty()) {
        emit errorOccurred(QStringLiteral("Screenshot not taken"), problem);
        return;
    }
    const QRect rect = capturedScreenRect();
    runAsync(
        this,
        [=]() -> QString {
            try {
                // LumaCapture's own windows are excluded from capture (WDA_EXCLUDEFROMCAPTURE),
                // so they are not part of the screenshot.
                capture::ScreenshotImage img;
                if (kind == SourceKind::Window)
                    img = capture::screenshotWindow(hwnd, cursor);
                else
                    img = capture::screenshotRect(RECT{rect.left(), rect.top(), rect.left() + rect.width(),
                                                       rect.top() + rect.height()},
                                                  cursor);
                const QImage q(reinterpret_cast<const uchar*>(img.pixels.data()), static_cast<int>(img.width),
                               static_cast<int>(img.height), static_cast<qsizetype>(img.width) * 4, QImage::Format_RGB32);
                if (!QDir().mkpath(dir))
                    return QStringLiteral("!Cannot create the folder %1").arg(QDir::toNativeSeparators(dir));
                if (!q.save(file, "PNG"))
                    return QStringLiteral("!Could not write %1").arg(QDir::toNativeSeparators(file));
                return file;
            } catch (const std::exception& e) {
                return QStringLiteral("!") + QString::fromUtf8(e.what());
            }
        },
        [this](const QString& result) {
            if (result.startsWith('!')) {
                emit errorOccurred(QStringLiteral("Screenshot failed"), result.mid(1));
            } else {
                log::info("Screenshot saved: {}", result.toStdString());
                emit screenshotSaved(result);
            }
        });
}

void RecordingController::scanForRecovery()
{
    const QString dir = m_settings.outputDir;
    runAsync(
        this,
        [dir]() -> QList<RecoveryCandidate> {
            QList<RecoveryCandidate> out;
            QDirIterator it(dir, {QStringLiteral("*.lumarec")}, QDir::Files);
            while (it.hasNext()) {
                const QString marker = it.next();
                QString media = marker;
                media.chop(8); // ".lumarec"
                if (QFileInfo::exists(media) && QFileInfo(media).size() > 0)
                    out.append({marker, media, QFileInfo(media).size()});
                else
                    QFile::remove(marker); // nothing to recover
            }
            return out;
        },
        [this](const QList<RecoveryCandidate>& items) {
            if (!items.isEmpty())
                emit recoveryFound(items);
        });
}

void RecordingController::recover(const QList<RecoveryCandidate>& items)
{
    struct RecoverResult {
        QList<RecordingEntry> ok;
        QStringList failed;
    };
    runAsync(
        this,
        [items]() -> RecoverResult {
            RecoverResult r;
            for (const RecoveryCandidate& c : items) {
                const QFileInfo fi(c.media);
                const QString out = fi.path() + '/' + fi.completeBaseName() + "-recovered.mkv";
                try {
                    mux::remux(c.media.toStdWString(), out.toStdWString());
                    QFile::remove(c.marker);
                    RecordingEntry e;
                    e.path = out;
                    e.created = QFileInfo(out).lastModified();
                    mux::MediaInfo mi;
                    if (mux::probeMedia(out.toStdWString(), mi)) {
                        e.durationSeconds = mi.durationSeconds;
                        e.width = mi.width;
                        e.height = mi.height;
                        e.fps = mi.fps;
                    }
                    e.sizeBytes = QFileInfo(out).size();
                    r.ok.append(e);
                } catch (const std::exception& e) {
                    r.failed << QStringLiteral("%1: %2").arg(fi.fileName(), QString::fromUtf8(e.what()));
                }
            }
            return r;
        },
        [this](const RecoverResult& r) {
            for (const RecordingEntry& e : r.ok)
                m_history.add(e);
            emit historyChanged();
            if (!r.ok.isEmpty())
                emit info(QStringLiteral("Recovered %1 recording(s). The original files were kept.").arg(r.ok.size()));
            if (!r.failed.isEmpty())
                emit errorOccurred(QStringLiteral("Recovery failed"), r.failed.join(QStringLiteral("\n")));
        });
}

} // namespace luma::app
