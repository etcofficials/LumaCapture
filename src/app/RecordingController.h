#pragma once

#include "AppSettings.h"
#include "History.h"
#include "OverlayRenderer.h"
#include "session/RecordingSession.h"
#include "session/RecordingState.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSize>
#include <QTimer>

#include <memory>

namespace luma::app {

class DeviceScanner;
class WebcamController;

struct RecoveryCandidate {
    QString marker;   // *.lumarec
    QString media;    // interrupted recording
    qint64 size = 0;
};

// Bridges the UI and the recording engine. Owns the RecordingSession; all
// blocking work (start, stop/finalise, MP4 remux, recovery, screenshots) runs
// on worker threads, results come back as signals on the UI thread.
// Every request goes through the RecordingState machine; invalid requests
// (double start, stop while stopping, ...) are ignored and logged.
class RecordingController : public QObject {
    Q_OBJECT
public:
    using State = session::RecState;

    RecordingController(AppSettings& settings, WebcamController& webcam, DeviceScanner& scanner, History& history,
                        QObject* parent = nullptr);
    ~RecordingController() override;

    State state() const { return m_state; }
    bool busy() const { return session::isBusy(m_state); }
    bool capturing() const { return session::isCapturing(m_state); }
    const session::SessionStatus& lastStatus() const { return m_lastStatus; }
    QString currentFile() const { return m_currentFile; }
    QString lastFile() const { return m_lastFile; }
    // Seconds spent in Stopping/Finalizing so far (for progress text).
    double finishingSeconds() const { return m_finishClock.isValid() ? m_finishClock.elapsed() / 1000.0 : 0; }

    void setWindowTarget(quintptr hwnd) { m_window = hwnd; }
    quintptr windowTarget() const { return m_window; }

    // Source/output geometry for the current settings (no device access).
    QSize sourceSize() const;
    QSize outputSize() const;
    QString sourceDescription() const;
    QString validateSource() const; // empty if the source can be recorded

    void start();
    void stop();
    void togglePause();
    void setMicMuted(bool muted);
    // Pushes composition / audio levels / mic processing to a running recording.
    void updateLive();

    void takeScreenshot();
    void scanForRecovery();
    void recover(const QList<RecoveryCandidate>& items);

signals:
    void stateChanged(luma::session::RecState state);
    void statusTick();
    void errorOccurred(const QString& title, const QString& message);
    void info(const QString& message);
    void recordingSaved(const QString& file, bool withErrors);
    void recoveryFound(const QList<luma::app::RecoveryCandidate>& items);
    void historyChanged();

private:
    bool apply(session::RecEvent e);
    session::SessionConfig buildConfig(QString& error);
    gpu::CompositionSettings buildComposition(int outW, int outH);
    QString makeOutputPath() const;
    int monitorIndex(QRect* rect) const;
    void pollStatus();

    AppSettings& m_settings;
    WebcamController& m_webcam;
    DeviceScanner& m_scanner;
    History& m_history;
    OverlayRenderer m_overlay;

    std::shared_ptr<session::RecordingSession> m_session;
    State m_state = State::Idle;
    session::SessionStatus m_lastStatus;
    QString m_currentFile, m_lastFile;
    quintptr m_window = 0;
    QSize m_outSize;
    QTimer m_statusTimer;
    QElapsedTimer m_finishClock;
    int m_diskCheckCounter = 0;
    bool m_stopRequestedByError = false;
};

} // namespace luma::app
