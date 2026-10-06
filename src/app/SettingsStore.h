#pragma once

#include "AppSettings.h"

#include <QObject>
#include <QTimer>

namespace luma::app {

// Owns the application settings. Every change goes through edit()/notify(): listeners
// get changed(scope) and the settings are saved shortly afterwards on the disk worker
// thread (never on the UI thread). Several panels can show the same setting; they
// refresh themselves from changed().
class SettingsStore : public QObject {
    Q_OBJECT
public:
    // What changed (bit flags), so listeners only do the work that is needed.
    enum Scope : unsigned {
        Source = 1u << 0,
        Video = 1u << 1,
        Audio = 1u << 2,
        Webcam = 1u << 3,
        Overlays = 1u << 4,
        Effects = 1u << 5,  // cursor effects, screen colour filters
        Output = 1u << 6,
        General = 1u << 7,
        Hotkeys = 1u << 8,
        Appearance = 1u << 9,
        Ui = 1u << 10,      // UI state only (tab, sort order)
        All = 0xFFFFu,
    };

    explicit SettingsStore(QString file, QObject* parent = nullptr);
    ~SettingsStore() override;

    const AppSettings& get() const { return m_s; }
    // Direct access for components that keep a reference (controllers); after changing
    // something through it, call notify() with the matching scope.
    AppSettings& ref() { return m_s; }

    template <typename F>
    void edit(unsigned scope, F&& change)
    {
        change(m_s);
        notify(scope);
    }
    void notify(unsigned scope);
    void replace(const AppSettings& s);

    // Automation runs (self-test, snapshots) never write the user's settings.
    void setPersistent(bool on) { m_persistent = on; }
    // Queues the final save (app exit); the caller waits on diskworker::shutdown().
    void saveNow();

signals:
    void changed(unsigned scope);

private:
    AppSettings m_s;
    QString m_file;
    QTimer m_saveTimer;
    bool m_persistent = true;
};

} // namespace luma::app
