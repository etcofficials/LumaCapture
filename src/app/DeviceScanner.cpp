#include "DeviceScanner.h"

#include "audio/AudioDevices.h"
#include "gpu/D3D11Device.h"
#include "util/Log.h"
#include "webcam/WebcamCapture.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <windows.h>
#include <dwmapi.h>

namespace luma::app {
namespace {

QString qs(const std::wstring& w) { return QString::fromStdWString(w); }

struct EnumCtx {
    QList<WindowEntry> out;
    DWORD ownPid = 0;
};

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp)
{
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr)
        return TRUE;
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW)
        return TRUE;
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked)
        return TRUE; // hidden UWP / other virtual desktop
    wchar_t title[512] = {};
    if (GetWindowTextW(hwnd, title, 512) <= 0)
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->ownPid)
        return TRUE;
    WindowEntry e;
    e.hwnd = reinterpret_cast<quintptr>(hwnd);
    e.title = QString::fromWCharArray(title);
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(hwnd, &r);
    e.rect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
    if (e.rect.width() < 40 || e.rect.height() < 30)
        return TRUE;
    if (HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH] = {};
        DWORD len = MAX_PATH;
        if (QueryFullProcessImageNameW(proc, 0, path, &len))
            e.process = QFileInfo(QString::fromWCharArray(path, static_cast<int>(len))).fileName();
        CloseHandle(proc);
    }
    ctx->out.append(e);
    return TRUE;
}

} // namespace

QString MonitorEntry::label() const
{
    QString name = deviceName;
    name.remove(QStringLiteral("\\\\.\\"));
    return QStringLiteral("%1 - %2x%3 @ %4 Hz%5")
        .arg(name)
        .arg(rect.width())
        .arg(rect.height())
        .arg(qRound(refreshHz))
        .arg(primary ? QStringLiteral(" (primary)") : QString());
}

QString CameraModeEntry::label() const
{
    const double fps = fpsDen ? static_cast<double>(fpsNum) / fpsDen : 0;
    return QStringLiteral("%1 x %2  @ %3 fps  (%4)")
        .arg(width)
        .arg(height)
        .arg(QString::number(fps, 'f', fps == static_cast<int>(fps) ? 0 : 2))
        .arg(format);
}

DeviceScanner::DeviceScanner(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<QList<luma::app::WindowEntry>>();
    qRegisterMetaType<QList<luma::app::CameraModeEntry>>();
}

void DeviceScanner::scanMonitors()
{
    QPointer<DeviceScanner> self(this);
    (void)QtConcurrent::run([self] {
        QList<MonitorEntry> list;
        try {
            const auto outs = gpu::enumerateOutputs();
            for (size_t i = 0; i < outs.size(); ++i) {
                MonitorEntry m;
                m.deviceName = qs(outs[i].deviceName);
                m.adapter = qs(outs[i].adapterName);
                const RECT& r = outs[i].desktopRect;
                m.rect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
                m.refreshHz = outs[i].refreshHz;
                m.primary = outs[i].primary;
                m.index = static_cast<int>(i);
                list.append(m);
            }
        } catch (const std::exception& e) {
            log::error("Display enumeration failed: {}", e.what());
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, list] {
            if (!self)
                return;
            self->m_monitors = list;
            emit self->monitorsReady();
        });
    });
}

void DeviceScanner::scanWindows(quintptr)
{
    QPointer<DeviceScanner> self(this);
    (void)QtConcurrent::run([self] {
        EnumCtx ctx;
        ctx.ownPid = GetCurrentProcessId();
        EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx));
        QList<WindowEntry> list = ctx.out;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, list] {
            if (self)
                emit self->windowsReady(list);
        });
    });
}

void DeviceScanner::scanAudio()
{
    QPointer<DeviceScanner> self(this);
    (void)QtConcurrent::run([self] {
        QList<AudioEntry> render, capture;
        try {
            for (const auto& d : audio::enumerateRenderDevices())
                render.append({qs(d.id), qs(d.name), d.isDefault});
            for (const auto& d : audio::enumerateCaptureDevices())
                capture.append({qs(d.id), qs(d.name), d.isDefault});
        } catch (const std::exception& e) {
            log::error("Audio device enumeration failed: {}", e.what());
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, render, capture] {
            if (!self)
                return;
            self->m_render = render;
            self->m_capture = capture;
            emit self->audioReady();
        });
    });
}

void DeviceScanner::scanCameras()
{
    QPointer<DeviceScanner> self(this);
    (void)QtConcurrent::run([self] {
        QList<CameraEntry> list;
        try {
            for (const auto& d : webcam::enumerateWebcams())
                list.append({qs(d.symbolicLink), qs(d.name)});
        } catch (const std::exception& e) {
            log::error("Camera enumeration failed: {}", e.what());
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, list] {
            if (!self)
                return;
            self->m_cameras = list;
            emit self->camerasReady();
        });
    });
}

void DeviceScanner::scanCameraModes(const QString& link)
{
    QPointer<DeviceScanner> self(this);
    (void)QtConcurrent::run([self, link] {
        QList<CameraModeEntry> list;
        QString error;
        // The camera may still be held for a moment by an instance that is shutting down.
        for (int attempt = 0; attempt < 6; ++attempt) {
            try {
                list.clear();
                error.clear();
                for (const auto& m : webcam::enumerateWebcamModes(link.toStdWString()))
                    list.append({m.width, m.height, m.fpsNum, m.fpsDen, QString::fromStdString(m.format)});
                break;
            } catch (const std::exception& e) {
                error = QString::fromUtf8(e.what());
                Sleep(300);
            }
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, link, list, error] {
            if (self)
                emit self->cameraModesReady(link, list, error);
        });
    });
}

} // namespace luma::app
