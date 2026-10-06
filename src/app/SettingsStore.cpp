#include "SettingsStore.h"

#include "DiskWorker.h"

namespace luma::app {

SettingsStore::SettingsStore(QString file, QObject* parent) : QObject(parent), m_file(std::move(file))
{
    m_s = loadSettings(m_file); // startup: before the window is shown
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(800);
    connect(&m_saveTimer, &QTimer::timeout, this, &SettingsStore::saveNow);
}

SettingsStore::~SettingsStore()
{
    if (m_saveTimer.isActive())
        saveNow();
}

void SettingsStore::notify(unsigned scope)
{
    m_saveTimer.start(); // debounced: a slider drag saves once, after it stops
    emit changed(scope);
}

void SettingsStore::replace(const AppSettings& s)
{
    m_s = s;
    notify(All);
}

void SettingsStore::saveNow()
{
    m_saveTimer.stop();
    if (!m_persistent)
        return;
    // Snapshot copy: the worker thread writes it while the UI keeps going.
    const AppSettings snapshot = m_s;
    const QString file = m_file;
    diskworker::post(QStringLiteral("settings"), [snapshot, file] { saveSettings(snapshot, file); });
}

} // namespace luma::app
