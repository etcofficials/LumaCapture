#include "WebcamController.h"

#include "util/Log.h"

#include <cstring>

namespace luma::app {

gpu::WebcamPlacement resolvedPlacement(const gpu::WebcamPlacement& p, double camAspect, int outW, int outH)
{
    gpu::WebcamPlacement r = p;
    const double wPx = std::clamp<double>(p.w, 0.03, 1.0) * outW;
    double hPx = p.shape == gpu::WebcamShape::Circle ? wPx : wPx / std::max(0.1, camAspect);
    hPx = std::min<double>(hPx, outH);
    r.w = static_cast<float>(wPx / outW);
    r.h = static_cast<float>(hPx / outH);
    r.x = std::clamp(p.x, 0.f, 1.f - r.w);
    r.y = std::clamp(p.y, 0.f, 1.f - r.h);
    if (p.shape == gpu::WebcamShape::Circle && camAspect > 1.0) {
        // Circle: crop the camera image to a centred square so the face is not squashed.
        const float extra = static_cast<float>((1.0 - 1.0 / camAspect) / 2.0);
        r.cropLeft = std::max(r.cropLeft, extra);
        r.cropRight = std::max(r.cropRight, extra);
    }
    return r;
}

WebcamController::WebcamController(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<luma::webcam::FrameStats>();
    // Poll at the camera rate; only new frames are converted (stale frames are skipped, never queued).
    m_previewTimer.setInterval(33);
    connect(&m_previewTimer, &QTimer::timeout, this, &WebcamController::poll);
    m_stateTimer.setInterval(500);
    connect(&m_stateTimer, &QTimer::timeout, this, [this] {
        const auto st = m_capture ? m_capture->state() : webcam::WebcamCapture::State::Stopped;
        if (st != m_lastState) {
            m_lastState = st;
            emit stateChanged();
        }
    });
    m_stateTimer.start();
    m_reapTimer.setInterval(50);
    connect(&m_reapTimer, &QTimer::timeout, this, &WebcamController::reapRetired);
}

WebcamController::~WebcamController()
{
    // App shutdown: the recording controller (which may reference the frames) is
    // already gone. Stopping is bounded (async reader), so joining here is safe.
    if (m_capture)
        m_capture->requestStop();
    for (auto& r : m_retiring)
        r->stop();
    if (m_capture)
        m_capture->stop();
}

void WebcamController::retireCurrent()
{
    if (!m_capture)
        return;
    m_previewTimer.stop();
    m_capture->requestStop(); // non-blocking; the camera thread tears down and exits by itself
    m_retiring.push_back(std::move(m_capture));
    m_lastSeq = m_lastRawSeq = 0;
    m_reapTimer.start();
    emit stateChanged();
}

void WebcamController::reapRetired()
{
    for (auto it = m_retiring.begin(); it != m_retiring.end();) {
        if ((*it)->finished()) {
            (*it)->stop(); // thread already finished: join returns immediately
            it = m_retiring.erase(it);
        } else {
            ++it;
        }
    }
    if (m_retiring.empty()) {
        m_reapTimer.stop();
        if (m_deferred) {
            const auto s = std::move(m_deferred);
            apply(*s);
        }
    }
}

void WebcamController::stopCamera()
{
    if (m_locked) {
        log::warn("Webcam: stop ignored while a recording uses the camera");
        return;
    }
    retireCurrent();
}

void WebcamController::pushFilters(const AppSettings& s)
{
    if (!m_capture)
        return;
    if (s.webcamBackground != m_backgroundPath) {
        m_backgroundPath = s.webcamBackground;
        m_background.reset();
        QImage img(m_backgroundPath);
        if (!img.isNull()) {
            img = img.convertToFormat(QImage::Format_RGB32);
            auto buf = std::make_shared<webcam::ImageBuffer>();
            buf->width = static_cast<unsigned>(img.width());
            buf->height = static_cast<unsigned>(img.height());
            buf->pixels.resize(static_cast<size_t>(img.width()) * img.height());
            for (int y = 0; y < img.height(); ++y)
                std::memcpy(buf->pixels.data() + static_cast<size_t>(y) * img.width(), img.constScanLine(y),
                            static_cast<size_t>(img.width()) * 4);
            m_background = buf;
        }
    }
    webcam::WebcamFilterSettings f = s.webcamFilters;
    f.background = m_background;
    m_capture->setFilters(f);
}

void WebcamController::apply(const AppSettings& s)
{
    m_previewSetting = s.webcamPreview;
    if (m_locked) {
        pushFilters(s); // recording in progress: never stop/restart the camera
        return;
    }
    const bool wantRunning = (s.webcamEnabled || m_previewRequested) && !s.webcamLink.isEmpty();
    const bool modeChanged = s.webcamLink != m_link || s.webcamMode.width != m_mode.width ||
                             s.webcamMode.height != m_mode.height || s.webcamMode.fpsNum != m_mode.fpsNum ||
                             s.webcamMode.fpsDen != m_mode.fpsDen || s.webcamMode.format != m_mode.format;
    m_enabled = s.webcamEnabled;

    if (!wantRunning) {
        m_deferred.reset();
        retireCurrent();
        m_link.clear();
        return;
    }
    if (!m_capture || modeChanged) {
        retireCurrent();
        if (!m_retiring.empty()) {
            // The previous instance still holds the device: start once it is released.
            m_deferred = std::make_unique<AppSettings>(s);
            m_link = s.webcamLink;
            m_mode = s.webcamMode;
            return;
        }
        m_capture = std::make_unique<webcam::WebcamCapture>();
        m_link = s.webcamLink;
        m_mode = s.webcamMode;
        pushFilters(s);
        m_capture->setCompareEnabled(m_compare);
        webcam::WebcamMode mode;
        mode.width = m_mode.width;
        mode.height = m_mode.height;
        mode.fpsNum = m_mode.fpsNum;
        mode.fpsDen = m_mode.fpsDen ? m_mode.fpsDen : 1;
        mode.format = m_mode.format.toStdString();
        m_capture->start(m_link.toStdWString(), mode);
        m_lastSeq = m_lastRawSeq = 0;
        emit stateChanged();
    } else {
        pushFilters(s);
    }
    if (m_previewSetting || m_previewRequested)
        m_previewTimer.start();
    else
        m_previewTimer.stop();
}

void WebcamController::setPreviewRequested(bool on)
{
    m_previewRequested = on;
    if (on && m_capture)
        m_previewTimer.start();
    else if (!on && !m_previewSetting)
        m_previewTimer.stop();
}

void WebcamController::setCompare(bool on)
{
    m_compare = on;
    if (m_capture)
        m_capture->setCompareEnabled(on);
}

void WebcamController::requestAutoAdjust()
{
    if (!m_capture)
        return;
    m_capture->requestAnalysis();
    m_analysisPending = true;
    if (!m_previewTimer.isActive())
        m_previewTimer.start(); // poll() collects the result
}

bool WebcamController::running() const
{
    return m_capture && m_capture->state() == webcam::WebcamCapture::State::Running;
}

QString WebcamController::stateText() const
{
    if (!m_capture)
        return m_retiring.empty() ? QStringLiteral("Camera off") : QStringLiteral("Releasing the camera...");
    switch (m_capture->state()) {
    case webcam::WebcamCapture::State::Starting: return QStringLiteral("Starting camera...");
    case webcam::WebcamCapture::State::Running: {
        const auto st = m_capture->stats();
        return QStringLiteral("%1 x %2  •  %3 fps").arg(st.width).arg(st.height).arg(st.fps, 0, 'f', 0);
    }
    case webcam::WebcamCapture::State::Error:
        return QStringLiteral("%1 Retrying...").arg(QString::fromStdString(m_capture->message()));
    default: return QStringLiteral("Camera off");
    }
}

std::vector<webcam::CameraControl> WebcamController::controls() const
{
    return m_capture ? m_capture->controls() : std::vector<webcam::CameraControl>{};
}

void WebcamController::setControl(const webcam::CameraControl& c)
{
    if (m_capture)
        m_capture->setControl(c);
}

webcam::CameraStats WebcamController::stats() const
{
    return m_capture ? m_capture->stats() : webcam::CameraStats{};
}

double WebcamController::aspect(const gpu::WebcamPlacement& p) const
{
    double w = m_frameW ? m_frameW : (m_mode.width ? m_mode.width : 16);
    double h = m_frameH ? m_frameH : (m_mode.height ? m_mode.height : 9);
    w *= std::max(0.1, 1.0 - p.cropLeft - p.cropRight);
    h *= std::max(0.1, 1.0 - p.cropTop - p.cropBottom);
    return w / std::max(1.0, h);
}

void WebcamController::poll()
{
    if (!m_capture)
        return;
    if (m_analysisPending) {
        webcam::FrameStats st;
        if (m_capture->takeAnalysis(st)) {
            m_analysisPending = false;
            emit autoAdjustReady(st);
        }
    }
    if (!m_previewSetting && !m_previewRequested)
        return;
    webcam::FrameExchange& ex = m_capture->frames();
    const uint64_t seq = ex.sequence();
    if (seq != m_lastSeq) {
        m_lastSeq = seq;
        QImage copy;
        ex.read([&](const webcam::ImageBuffer& b) {
            m_frameW = b.width;
            m_frameH = b.height;
            // Wrap the buffer, then make a small scaled copy (the buffer is released right after).
            const QImage view(reinterpret_cast<const uchar*>(b.pixels.data()), static_cast<int>(b.width),
                              static_cast<int>(b.height), static_cast<qsizetype>(b.width) * 4, QImage::Format_ARGB32);
            copy = view.width() > 640 ? view.scaledToWidth(640, Qt::FastTransformation) : view.copy();
        });
        if (!copy.isNull())
            emit previewFrame(copy);
    }
    if (m_compare) {
        webcam::FrameExchange& raw = m_capture->rawPreview();
        const uint64_t rs = raw.sequence();
        if (rs != m_lastRawSeq) {
            m_lastRawSeq = rs;
            QImage copy;
            raw.read([&](const webcam::ImageBuffer& b) {
                copy = QImage(reinterpret_cast<const uchar*>(b.pixels.data()), static_cast<int>(b.width),
                              static_cast<int>(b.height), static_cast<qsizetype>(b.width) * 4, QImage::Format_RGB32)
                           .copy();
            });
            if (!copy.isNull())
                emit originalFrame(copy);
        }
    }
}

} // namespace luma::app
