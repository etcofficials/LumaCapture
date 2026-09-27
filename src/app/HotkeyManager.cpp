#include "HotkeyManager.h"

#include <QCoreApplication>
#include <QKeyCombination>

#include <windows.h>

namespace luma::app {
namespace {

constexpr int kBaseId = 0x4C43; // 'LC'

unsigned vkFromQt(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return static_cast<unsigned>('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return static_cast<unsigned>('0' + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return static_cast<unsigned>(VK_F1 + (key - Qt::Key_F1));
    switch (key) {
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Pause: return VK_PAUSE;
    case Qt::Key_Print: return VK_SNAPSHOT;
    case Qt::Key_ScrollLock: return VK_SCROLL;
    case Qt::Key_Minus: return VK_OEM_MINUS;
    case Qt::Key_Equal: return VK_OEM_PLUS;
    case Qt::Key_Comma: return VK_OEM_COMMA;
    case Qt::Key_Period: return VK_OEM_PERIOD;
    default: return 0;
    }
}

} // namespace

HotkeyManager::HotkeyManager(QObject* parent) : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager()
{
    unregisterAll();
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool HotkeyManager::toNative(const QKeySequence& seq, unsigned& mods, unsigned& vk)
{
    if (seq.isEmpty())
        return false;
    const QKeyCombination kc = seq[0];
    vk = vkFromQt(kc.key());
    mods = MOD_NOREPEAT;
    const Qt::KeyboardModifiers m = kc.keyboardModifiers();
    if (m & Qt::ControlModifier) mods |= MOD_CONTROL;
    if (m & Qt::AltModifier) mods |= MOD_ALT;
    if (m & Qt::ShiftModifier) mods |= MOD_SHIFT;
    if (m & Qt::MetaModifier) mods |= MOD_WIN;
    return vk != 0;
}

void HotkeyManager::unregisterAll()
{
    for (int id : m_registered)
        UnregisterHotKey(reinterpret_cast<HWND>(m_hwnd), id);
    m_registered.clear();
}

QStringList HotkeyManager::registerAll(const AppSettings& s)
{
    unregisterAll();
    QStringList conflicts;
    QList<QKeySequence> seen;
    for (int i = 0; i < static_cast<int>(HotkeyAction::Count); ++i) {
        const QKeySequence& seq = s.hotkeys[i];
        if (seq.isEmpty())
            continue;
        const QString name = AppSettings::hotkeyName(static_cast<HotkeyAction>(i));
        const QString text = seq.toString(QKeySequence::NativeText);
        if (seen.contains(seq)) {
            conflicts << QStringLiteral("%1: %2 is assigned twice in LumaCapture").arg(name, text);
            continue;
        }
        seen << seq;
        unsigned mods = 0, vk = 0;
        if (!toNative(seq, mods, vk)) {
            conflicts << QStringLiteral("%1: %2 cannot be used as a global hotkey").arg(name, text);
            continue;
        }
        const int id = kBaseId + i;
        if (!RegisterHotKey(reinterpret_cast<HWND>(m_hwnd), id, mods, vk)) {
            conflicts << QStringLiteral("%1: %2 is already used by another program").arg(name, text);
            continue;
        }
        m_registered << id;
    }
    return conflicts;
}

bool HotkeyManager::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*)
{
    if (eventType != "windows_generic_MSG")
        return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_HOTKEY)
        return false;
    const int id = static_cast<int>(msg->wParam) - kBaseId;
    if (id < 0 || id >= static_cast<int>(HotkeyAction::Count))
        return false;
    emit triggered(static_cast<HotkeyAction>(id));
    return true;
}

} // namespace luma::app
