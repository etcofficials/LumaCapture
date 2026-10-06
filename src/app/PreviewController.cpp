#include "PreviewController.h"

#include "RecordingController.h"
#include "session/PreviewExchange.h"
#include "session/RecordingSession.h"
#include "util/Log.h"

#include <QCoreApplication>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <cstring>

namespace luma::app {

PreviewController::PreviewController(RecordingController& recording, QObject* parent)
    : QObject(parent), m_recording(recording), m_exchange(std::make_shared<session::PreviewExchange>())
{
    m_recording.setPreviewExchange(m_exchange);
    m_poll.setInterval(50); // checks for a new picture; copies only when one arrived
    connect(&m_poll, &QTimer::timeout, this, &PreviewController::poll);
    m_restart.setSingleShot(true);
    m_restart.setInterval(400);
    connect(&m_restart, &QTimer::timeout, this, [this] {
        ++m_generation; // a start still in flight is stale now
        m_needRestart = true;
        if (m_idle)
            stopSession(std::exchange(m_idle, nullptr));
        checkReleased();
    });
}

PreviewController::~PreviewController()
{
    m_poll.stop();
    m_restart.stop();
    // App exit: stop the idle preview here (bounded - its capture thread ends within one tick).
    if (m_idle) {
        try {
            m_idle->stop();
        } catch (...) {
        }
        m_idle.reset();
    }
}

void PreviewController::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    reconcile();
}

void PreviewController::setVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;
    reconcile();
}

void PreviewController::setViewSize(QSize pixels)
{
    // Never more than 960x540: it is a preview, and the copy happens on the UI thread.
    pixels = pixels.boundedTo(QSize(960, 540));
    if (pixels == m_viewSize)
        return;
    m_viewSize = pixels;
    reconcile();
}

void PreviewController::setRecording(bool recording, bool framesWhileRecording)
{
    m_recordingActive = recording;
    m_framesWhileRecording = framesWhileRecording;
    if (recording && !framesWhileRecording)
        setMessage(QStringLiteral("Preview paused while recording (saves CPU)"));
    reconcile();
}

void PreviewController::invalidate()
{
    if (!m_recordingActive)
        m_restart.start();
}

void PreviewController::releaseForRecording(std::function<void()> then)
{
    m_recordingActive = true; // no idle session while a recording starts or runs
    m_restart.stop();
    ++m_generation;
    m_waiters.push_back(std::move(then));
    if (m_idle)
        stopSession(std::exchange(m_idle, nullptr));
    checkReleased();
}

bool PreviewController::wantIdleSession() const
{
    return m_enabled && m_visible && !m_recordingActive && m_viewSize.width() >= 16 && m_viewSize.height() >= 16;
}

void PreviewController::reconcile()
{
    const bool watching = m_enabled && m_visible && m_viewSize.width() >= 16 && m_viewSize.height() >= 16;
    if (watching)
        m_exchange->requestSize(static_cast<unsigned>(m_viewSize.width()), static_cast<unsigned>(m_viewSize.height()));
    else
        m_exchange->requestSize(0, 0); // the capture thread stops rendering preview images
    if (watching && (!m_recordingActive || m_framesWhileRecording)) {
        if (!m_poll.isActive())
            m_poll.start();
    } else {
        m_poll.stop();
    }
    if (!m_enabled)
        setMessage(QStringLiteral("Live preview is off"));

    if (wantIdleSession()) {
        if (!m_idle && !m_starting && m_pendingStops == 0)
            startIdle();
    } else if (m_idle) {
        stopSession(std::exchange(m_idle, nullptr));
    }
}

void PreviewController::startIdle()
{
    QString error;
    session::SessionConfig cfg = m_recording.buildPreviewConfig(error);
    if (!error.isEmpty()) {
        setMessage(error);
        return;
    }
    std::shared_ptr<session::RecordingSession> s;
    try {
        s = std::make_shared<session::RecordingSession>(std::move(cfg));
    } catch (const std::exception& e) {
        setMessage(QStringLiteral("Preview unavailable: %1").arg(QString::fromUtf8(e.what())));
        return;
    }
    m_starting = true;
    const int gen = ++m_generation;
    if (m_message.isEmpty() || m_message.startsWith(QStringLiteral("Live preview is off")))
        setMessage(QStringLiteral("Starting preview..."));
    QPointer<PreviewController> self(this);
    (void)QtConcurrent::run([s, self, gen] {
        QString err;
        try {
            s->start();
        } catch (const std::exception& e) {
            err = QString::fromUtf8(e.what());
        } catch (...) {
            err = QStringLiteral("unexpected error");
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, s, gen, err] {
            if (!self) {
                if (err.isEmpty())
                    (void)QtConcurrent::run([s] { s->stop(); });
                return;
            }
            self->m_starting = false;
            if (!err.isEmpty()) {
                log::warn("Live preview could not start: {}", err.toStdString());
                self->setMessage(QStringLiteral("Preview unavailable: %1").arg(err));
                self->checkReleased();
                return;
            }
            if (gen != self->m_generation || !self->wantIdleSession()) {
                self->stopSession(s); // no longer wanted (source changed, recording started, hidden)
                return;
            }
            self->m_idle = s;
        });
    });
}

void PreviewController::stopSession(std::shared_ptr<session::RecordingSession> s)
{
    if (!s)
        return;
    ++m_pendingStops;
    QPointer<PreviewController> self(this);
    (void)QtConcurrent::run([s, self] {
        try {
            s->stop();
        } catch (...) {
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self] {
            if (!self)
                return;
            --self->m_pendingStops;
            self->checkReleased();
        });
    });
}

void PreviewController::checkReleased()
{
    if (m_pendingStops > 0 || m_idle || m_starting)
        return;
    if (!m_waiters.empty()) {
        auto waiters = std::move(m_waiters);
        m_waiters.clear();
        for (auto& w : waiters)
            if (w)
                w();
    }
    if (m_needRestart) {
        m_needRestart = false;
        reconcile();
    }
}

void PreviewController::poll()
{
    unsigned w = 0, h = 0;
    if (!m_exchange->take(m_seq, m_pixels, w, h)) {
        if (m_idle && m_idle->failed()) {
            setMessage(QStringLiteral("Preview stopped: %1").arg(QString::fromStdString(m_idle->errorMessage())));
            stopSession(std::exchange(m_idle, nullptr));
        }
        return;
    }
    QImage img(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGB32);
    for (unsigned y = 0; y < h; ++y)
        std::memcpy(img.scanLine(static_cast<int>(y)), m_pixels.data() + static_cast<size_t>(y) * w,
                    static_cast<size_t>(w) * 4);
    setMessage({});
    emit frameReady(img);
}

void PreviewController::setMessage(const QString& m)
{
    if (m == m_message)
        return;
    m_message = m;
    emit messageChanged(m);
}

} // namespace luma::app
