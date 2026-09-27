#include "AudioMonitor.h"

#include <QtConcurrent/QtConcurrentRun>

namespace luma::app {

AudioMonitor::AudioMonitor(QObject* parent) : QObject(parent)
{
    m_timer.setInterval(66);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        emit levels(m_system ? m_system->takePeak() : 0.f, m_mic ? m_mic->takePeak() : 0.f);
    });
}

AudioMonitor::~AudioMonitor()
{
    stop();
}

void AudioMonitor::start(bool system, const QString& systemDevice, bool mic, const QString& micDevice)
{
    stop();
    if (system) {
        m_system = std::make_unique<audio::WasapiSource>(
            audio::AudioSourceConfig{true, systemDevice.toStdWString(), 0}, nullptr, 0);
        m_system->start();
    }
    if (mic) {
        m_mic = std::make_unique<audio::WasapiSource>(audio::AudioSourceConfig{false, micDevice.toStdWString(), 0},
                                                      nullptr, 0);
        m_mic->start();
    }
    if (active())
        m_timer.start();
}

void AudioMonitor::stop()
{
    m_timer.stop();
    // Joining the device threads can take up to ~100 ms each; do it off the UI thread.
    std::shared_ptr<audio::WasapiSource> sys(std::move(m_system)), mic(std::move(m_mic));
    if (sys || mic)
        (void)QtConcurrent::run([sys, mic] {
            if (sys)
                sys->stop();
            if (mic)
                mic->stop();
        });
    emit levels(0.f, 0.f);
}

} // namespace luma::app
