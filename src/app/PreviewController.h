#pragma once

#include <QImage>
#include <QObject>
#include <QSize>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace luma::session {
class PreviewExchange;
class RecordingSession;
} // namespace luma::session

namespace luma::app {

class RecordingController;

// Live preview of what will be recorded (screen + overlays + cursor effects).
//  * idle: a preview-only session (capture + GPU composition, no encoder, no audio,
//    no file) at a low frame rate - only while the preview is visible;
//  * recording: the recording session publishes small copies of its own frames.
// Sessions start/stop on worker threads; the UI thread only copies a small image
// when a new one is available (polled at most 20 times per second).
class PreviewController : public QObject {
    Q_OBJECT
public:
    explicit PreviewController(RecordingController& recording, QObject* parent = nullptr);
    ~PreviewController() override;

    std::shared_ptr<session::PreviewExchange> exchange() const { return m_exchange; }

    void setEnabled(bool on);       // user setting "Live preview"
    void setVisible(bool visible);  // the preview widget is on screen (window not minimised, Record page)
    void setViewSize(QSize pixels); // wanted image size (device pixels)
    // A recording is starting/running (no idle session then). `framesWhileRecording`:
    // whether the recording publishes preview frames.
    void setRecording(bool recording, bool framesWhileRecording);
    // Source / size / composition changed: rebuild the idle session (debounced).
    void invalidate();
    // Stops the idle preview and calls `then` (UI thread) once the capture is released.
    void releaseForRecording(std::function<void()> then);

    QString message() const { return m_message; }

signals:
    void frameReady(const QImage& image);
    void messageChanged(const QString& message); // empty = frames are flowing

private:
    void poll();
    void reconcile();
    void startIdle();
    void stopSession(std::shared_ptr<session::RecordingSession> s);
    void checkReleased();
    void setMessage(const QString& m);
    bool wantIdleSession() const;

    RecordingController& m_recording;
    std::shared_ptr<session::PreviewExchange> m_exchange;
    std::shared_ptr<session::RecordingSession> m_idle; // running idle preview session
    std::vector<std::function<void()>> m_waiters;     // releaseForRecording callbacks
    std::vector<uint32_t> m_pixels;
    QTimer m_poll;
    QTimer m_restart;
    QSize m_viewSize;
    QString m_message;
    uint64_t m_seq = 0;
    int m_generation = 0;   // invalidates results of stale start requests
    int m_pendingStops = 0;
    bool m_enabled = true;
    bool m_visible = false;
    bool m_recordingActive = false;
    bool m_framesWhileRecording = true;
    bool m_starting = false;
    bool m_needRestart = false;
};

} // namespace luma::app
