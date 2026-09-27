#pragma once

#include "audio/WasapiSource.h"

#include <QObject>
#include <QTimer>

#include <memory>

namespace luma::app {

// Level meters while NOT recording ("Test audio"): opens the selected devices
// in meter-only mode (nothing is stored). Off by default so the microphone is
// not held open (and Windows' mic indicator stays off) unless the user asks.
class AudioMonitor : public QObject {
    Q_OBJECT
public:
    explicit AudioMonitor(QObject* parent = nullptr);
    ~AudioMonitor() override;

    void start(bool system, const QString& systemDevice, bool mic, const QString& micDevice);
    void stop();
    bool active() const { return m_system || m_mic; }

signals:
    void levels(float systemPeak, float micPeak);

private:
    std::unique_ptr<audio::WasapiSource> m_system, m_mic;
    QTimer m_timer;
};

} // namespace luma::app
