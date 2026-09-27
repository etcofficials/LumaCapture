#pragma once

#include <QList>
#include <QObject>
#include <QRect>
#include <QString>

namespace luma::app {

struct MonitorEntry {
    QString deviceName; // \\.\DISPLAYn
    QString adapter;
    QRect rect;         // physical desktop pixels
    double refreshHz = 0;
    bool primary = false;
    int index = 0;      // index in gpu::enumerateOutputs()
    QString label() const;
};

struct WindowEntry {
    quintptr hwnd = 0;
    QString title;
    QString process;
    QRect rect;
};

struct AudioEntry {
    QString id;
    QString name;
    bool isDefault = false;
};

struct CameraEntry {
    QString link;
    QString name;
};

struct CameraModeEntry {
    unsigned width = 0, height = 0, fpsNum = 0, fpsDen = 1;
    QString format;
    QString label() const;
};

// All device/window enumeration runs on worker threads (QtConcurrent);
// results arrive as queued signals on the UI thread.
class DeviceScanner : public QObject {
    Q_OBJECT
public:
    explicit DeviceScanner(QObject* parent = nullptr);

    void scanMonitors();
    void scanWindows(quintptr excludeProcessWindowsOf = 0);
    void scanAudio();
    void scanCameras();
    void scanCameraModes(const QString& link);

    const QList<MonitorEntry>& monitors() const { return m_monitors; }
    const QList<AudioEntry>& renderDevices() const { return m_render; }
    const QList<AudioEntry>& captureDevices() const { return m_capture; }
    const QList<CameraEntry>& cameras() const { return m_cameras; }

signals:
    void monitorsReady();
    void windowsReady(QList<luma::app::WindowEntry> windows);
    void audioReady();
    void camerasReady();
    void cameraModesReady(QString link, QList<luma::app::CameraModeEntry> modes, QString error);

private:
    QList<MonitorEntry> m_monitors;
    QList<AudioEntry> m_render, m_capture;
    QList<CameraEntry> m_cameras;
};

} // namespace luma::app
