#pragma once

#include "audio/MicProcessor.h"
#include "gpu/CompositionSettings.h"
#include "webcam/WebcamFilters.h"

#include <QColor>
#include <QKeySequence>
#include <QList>
#include <QRect>
#include <QString>

namespace luma::app {

enum class SourceKind { Display = 0, Window = 1, Region = 2 };
enum class ResolutionPreset { Native = 0, P1080 = 1, P900 = 2, P720 = 3, P480 = 4 };
enum class QualityPreset { SmallFile = 0, Balanced = 1, HighQuality = 2, Custom = 3 };
enum class Container { Mkv = 0, Mp4 = 1 };
enum class Theme { Dark = 0, Light = 1 };
enum class HotkeyAction { RecordToggle = 0, Stop, PauseResume, MicMute, Screenshot, Count };

struct OverlayItem {
    enum class Type { Text = 0, Image = 1, Rectangle = 2 };
    Type type = Type::Text;
    bool enabled = true;
    QString name;
    // Position/size as fractions of the output frame.
    double x = 0.05, y = 0.05, w = 0.2, h = 0.1;
    double opacity = 1.0;
    // Text
    QString text = QStringLiteral("Your text");
    QString fontFamily = QStringLiteral("Segoe UI");
    int fontSize = 40;          // pixels at 1080p output (scaled with the output height)
    bool bold = true;
    QColor color = Qt::white;
    bool outline = true;
    QColor outlineColor = QColor(0, 0, 0);
    int outlineWidth = 3;
    bool shadow = true;
    // Image
    QString imagePath;
    // Rectangle / border
    QColor fillColor = QColor(0, 0, 0, 120);
    int borderWidth = 0;
    QColor borderColor = Qt::white;
    int cornerRadius = 8;
};

struct WatermarkSettings {
    bool enabled = false;
    QString text = QStringLiteral("LumaCapture");
    int corner = 3; // 0 TL, 1 TR, 2 BL, 3 BR
    double opacity = 0.5;
    int fontSize = 24;
};

struct WebcamModeSetting {
    unsigned width = 0, height = 0, fpsNum = 30, fpsDen = 1;
    QString format;
    bool valid() const { return width > 0 && height > 0; }
};

// Everything the user can configure; persisted in an INI file next to the exe.
struct AppSettings {
    // General & output
    QString outputDir;
    QString screenshotDir;
    QString namePattern = QStringLiteral("LumaCapture_{date}_{time}");
    Container container = Container::Mkv;
    bool keepMkvAfterMp4 = true;
    Theme theme = Theme::Dark;
    int countdownSeconds = 0;
    bool minimizeOnRecord = false;
    bool showRecordingBar = true;
    bool minimizeToTray = false;
    bool excludeOwnWindows = true;
    int lowSpaceWarnMB = 2000;
    QString profile = QStringLiteral("Balanced");

    // Source
    SourceKind sourceKind = SourceKind::Display;
    QString displayName;        // \\.\DISPLAYn; empty = primary
    QString windowTitle;        // last chosen window (runtime handle is kept separately)
    QRect region;               // physical screen pixels
    bool captureCursor = true;
    int cropLeft = 0, cropTop = 0, cropRight = 0, cropBottom = 0; // pixels (display/window)
    gpu::CursorEffects cursor;

    // Video
    ResolutionPreset resolution = ResolutionPreset::Native;
    int fps = 30;
    QualityPreset quality = QualityPreset::Balanced;
    int crf = 23;
    QString x264Preset = QStringLiteral("ultrafast");
    double keyframeSeconds = 2.0;
    int encoderThreads = 3;
    bool cpuConvert = false;
    int queueCapacity = 8;
    gpu::VideoFilterSettings videoFilters;

    // Audio
    bool systemAudio = true;
    QString systemDevice;       // empty = default
    double systemVolume = 1.0;
    bool microphone = false;
    QString micDevice;          // empty = default
    double micVolume = 1.0;
    bool micMuted = false;
    audio::MicFilterSettings micFilters;
    int micDelayMs = 0;
    int audioBitrate = 160;
    bool separateTracks = false;

    // Webcam
    bool webcamEnabled = false;
    QString webcamLink;
    QString webcamName;
    WebcamModeSetting webcamMode;
    bool webcamPreview = true;
    gpu::WebcamPlacement webcamPlacement; // h is derived from the camera aspect at render time
    webcam::WebcamFilterSettings webcamFilters; // background image is loaded from webcamBackground
    QString webcamBackground;

    // Overlays
    QList<OverlayItem> overlays;
    WatermarkSettings watermark;

    // Hotkeys
    QKeySequence hotkeys[static_cast<int>(HotkeyAction::Count)];

    // Screenshots
    bool screenshotCursor = true;

    int crfForQuality() const;
    static QString hotkeyName(HotkeyAction a);
};

AppSettings loadSettings(const QString& iniPath);
void saveSettings(const AppSettings& s, const QString& iniPath);

QStringList profileNames();
// Applies a named profile on top of the current settings (only the fields the profile defines).
void applyProfile(AppSettings& s, const QString& name);
QString profileDescription(const QString& name);

// Webcam filter presets ("Natural", "Brighter", "Warm", "Cool", "Monochrome", "Low light").
QStringList webcamFilterPresetNames();
void applyWebcamFilterPreset(webcam::WebcamFilterSettings& f, const QString& name);

} // namespace luma::app
