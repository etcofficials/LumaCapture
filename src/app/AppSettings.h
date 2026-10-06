#pragma once

#include "audio/MicProcessor.h"
#include "gpu/CompositionSettings.h"
#include "webcam/WebcamFilters.h"

#include <QColor>
#include <QKeySequence>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

namespace luma::app {

// Game: records the whole monitor the chosen game window is on (Desktop Duplication),
// which works for full-screen and borderless games and shows no capture border.
enum class SourceKind { Display = 0, Window = 1, Region = 2, Game = 3 };
enum class ResolutionPreset { Native = 0, P1080 = 1, P900 = 2, P720 = 3, P480 = 4 };
enum class QualityPreset { Low = 0, Balanced = 1, High = 2, VeryHigh = 3, Custom = 4 };
enum class Container { Mkv = 0, Mp4 = 1 };
enum class Theme { Dark = 0, Light = 1 };
enum class HotkeyAction { RecordToggle = 0, Stop, PauseResume, MicMute, Screenshot, Count };
enum class LibrarySort { Newest = 0, Oldest = 1, Size = 2, Duration = 3 };

// Frame rates offered in the UI.
inline constexpr int kFrameRates[] = {24, 25, 30, 50, 60};

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

// Everything the user can configure; persisted in settings.ini in the data folder.
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
    bool recordingHud = false;      // small always-on-top control bar while recording (off by default)
    bool minimizeToTray = false;
    bool excludeOwnWindows = true;  // keep LumaCapture's own windows out of recordings/screenshots
    int lowSpaceWarnMB = 2000;
    QString profile = QStringLiteral("Balanced");
    bool checkRecoveryOnStart = true;
    bool warnFallingBehind = true;

    // Live preview & diagnostics
    bool livePreview = true;
    int previewFps = 10;            // 2..15; the preview is a low-rate view, not a monitor
    bool previewWhileRecording = true;
    bool diagnostics = false;       // performance panel (off by default)

    // Source
    SourceKind sourceKind = SourceKind::Display;
    QString displayName;        // \\.\DISPLAYn; empty = primary
    QString windowTitle;        // last chosen window (runtime handle is kept separately)
    QString gameTitle;          // last chosen game window
    QRect region;               // physical screen pixels
    bool captureCursor = true;
    int cropLeft = 0, cropTop = 0, cropRight = 0, cropBottom = 0; // pixels (display/window)
    gpu::CursorEffects cursor;

    // Video
    ResolutionPreset resolution = ResolutionPreset::Native;
    int fps = 30;
    QualityPreset quality = QualityPreset::Balanced;
    int crf = 21;                                 // used by QualityPreset::Custom
    QString x264Preset = QStringLiteral("auto");  // auto | ultrafast | superfast | veryfast
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

    // UI state
    int contextTab = 0;               // right-hand panel tab on the Record page
    bool advancedVideo = false;       // show CRF / preset / threads in the Video panel
    LibrarySort librarySort = LibrarySort::Newest;
    bool libraryCopyOnImport = false; // Import copies files into the recordings folder

    static QString hotkeyName(HotkeyAction a);
};

// x264 parameters derived from the quality settings. The mapping comes from the
// encoder study on the target PC (docs/TESTING.md): on a 4-core Sandy Bridge,
// "ultrafast" with a lower CRF gives the best quality per CPU second; slower
// presets cost 40-80 % more CPU and scored lower at the same CRF.
struct EncoderChoice {
    QString preset = QStringLiteral("ultrafast");
    int crf = 21;
    QString x264Params;       // extra x264 options ("key=value:key=value")
    QString description() const;
};
EncoderChoice resolveEncoder(const AppSettings& s);

enum class PerfLevel { Light = 0, Moderate = 1, Heavy = 2, VeryHeavy = 3 };
// Rough CPU load of a recording relative to 1080p30 "ultrafast" (the heaviest
// configuration that stayed real-time on the target PC, also when it throttled).
struct PerfEstimate {
    PerfLevel level = PerfLevel::Moderate;
    double load = 1.0;
    QString label() const;    // "Light", "Moderate", "Heavy", "Very heavy"
    QString advice() const;   // empty when no warning is needed
};
PerfEstimate estimatePerformance(QSize output, int fps, const EncoderChoice& enc);

QString qualityName(QualityPreset q);
QString resolutionName(ResolutionPreset r);
QString sourceKindName(SourceKind k);
int presetHeight(ResolutionPreset p);
// Output size for a source size: keeps the aspect ratio, never upscales, even sizes.
QSize scaledOutputSize(QSize source, ResolutionPreset p);

AppSettings loadSettings(const QString& iniPath);
void saveSettings(const AppSettings& s, const QString& iniPath);
// Fresh defaults (default folders and hotkeys filled in); touches no file.
AppSettings defaultSettings();

QStringList profileNames();
// Applies a named profile on top of the current settings (only the fields the profile defines).
void applyProfile(AppSettings& s, const QString& name);
QString profileDescription(const QString& name);

// Webcam filter presets ("Natural", "Brighter", "Warm", "Cool", "Monochrome", "Low light").
QStringList webcamFilterPresetNames();
void applyWebcamFilterPreset(webcam::WebcamFilterSettings& f, const QString& name);

} // namespace luma::app
