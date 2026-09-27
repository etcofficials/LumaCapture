#pragma once

#include "AppSettings.h"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QStringList>

namespace luma::app {

// System-wide hotkeys via RegisterHotKey, delivered as WM_HOTKEY to the main
// window. Registration failures (combination used by another program) and
// duplicates inside LumaCapture are reported as conflicts.
class HotkeyManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit HotkeyManager(QObject* parent = nullptr);
    ~HotkeyManager() override;

    void setWindow(quintptr hwnd) { m_hwnd = hwnd; }
    // Re-registers every hotkey; returns human-readable conflicts (empty = all good).
    QStringList registerAll(const AppSettings& s);
    void unregisterAll();

    // Checks a single binding without registering it (for the settings page).
    static bool toNative(const QKeySequence& seq, unsigned& mods, unsigned& vk);

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void triggered(luma::app::HotkeyAction action);

private:
    quintptr m_hwnd = 0;
    QList<int> m_registered;
};

} // namespace luma::app
