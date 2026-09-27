#pragma once

#include "AppSettings.h"
#include "webcam/WebcamCapture.h"

#include <QImage>
#include <QObject>
#include <QTimer>

#include <memory>
#include <vector>

namespace luma::app {

// Owns the camera (core WebcamCapture on its own thread) for the whole app:
// the same processed frames feed the live preview and the recording.
// Never blocks the UI thread: a camera being stopped is "retired" (asked to
// stop, then destroyed once its thread has finished); a restart waits until
// the retired instance has released the device.
class WebcamController : public QObject {
    Q_OBJECT
public:
    explicit WebcamController(QObject* parent = nullptr);
    ~WebcamController() override;

    // Starts/stops/restarts the camera to match the settings.
    void apply(const AppSettings& s);
    void setPreviewRequested(bool on); // settings page keeps the camera on for configuring
    void stopCamera();
    // While a recording uses the camera's frames, the camera must not be stopped or
    // restarted (the session holds a pointer to its frame exchange); only filters change.
    void setLocked(bool locked) { m_locked = locked; }
    bool busyReleasing() const { return !m_retiring.empty(); }

    bool running() const;
    QString stateText() const;
    webcam::FrameExchange* frames() { return m_capture ? &m_capture->frames() : nullptr; }
    std::vector<webcam::CameraControl> controls() const;
    void setControl(const webcam::CameraControl& c);
    webcam::CameraStats stats() const;
    // Camera aspect after crop (width / height), 16:9 if unknown.
    double aspect(const gpu::WebcamPlacement& p) const;

    void setCompare(bool on);                 // also deliver the unprocessed image
    void requestAutoAdjust();                 // analyses the next frame -> autoAdjustReady

signals:
    void previewFrame(const QImage& frame);   // processed, scaled copy, at the camera rate (<= 30 fps)
    void originalFrame(const QImage& frame);  // unprocessed copy (only while compare is on)
    void stateChanged();
    void autoAdjustReady(const luma::webcam::FrameStats& stats);

private:
    void poll();
    void pushFilters(const AppSettings& s);
    void retireCurrent();
    void reapRetired();

    std::unique_ptr<webcam::WebcamCapture> m_capture;
    std::vector<std::unique_ptr<webcam::WebcamCapture>> m_retiring;
    std::unique_ptr<AppSettings> m_deferred; // apply() postponed until retired cameras released the device
    QString m_link;
    WebcamModeSetting m_mode;
    bool m_enabled = false;
    bool m_locked = false;
    bool m_previewSetting = true;
    bool m_previewRequested = false;
    bool m_compare = false;
    bool m_analysisPending = false;
    QString m_backgroundPath;
    std::shared_ptr<const webcam::ImageBuffer> m_background;
    QTimer m_previewTimer;
    QTimer m_stateTimer;
    QTimer m_reapTimer;
    uint64_t m_lastSeq = 0;
    uint64_t m_lastRawSeq = 0;
    webcam::WebcamCapture::State m_lastState = webcam::WebcamCapture::State::Stopped;
    unsigned m_frameW = 0, m_frameH = 0;
};

// Webcam placement with the height derived from the camera aspect ratio for an output size.
gpu::WebcamPlacement resolvedPlacement(const gpu::WebcamPlacement& p, double camAspect, int outW, int outH);

} // namespace luma::app

Q_DECLARE_METATYPE(luma::webcam::FrameStats)
