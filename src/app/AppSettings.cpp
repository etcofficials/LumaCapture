#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace luma::app {
namespace {

QString colorToString(const gpu::Rgba& c)
{
    return QColor::fromRgbF(c.r, c.g, c.b, c.a).name(QColor::HexArgb);
}

gpu::Rgba colorFromString(const QString& s, const gpu::Rgba& fallback)
{
    const QColor c(s);
    if (!c.isValid())
        return fallback;
    return gpu::Rgba{static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF()),
                     static_cast<float>(c.alphaF())};
}

QString defaultHotkey(HotkeyAction a)
{
    switch (a) {
    case HotkeyAction::RecordToggle: return QStringLiteral("Ctrl+Alt+R");
    case HotkeyAction::Stop:         return QStringLiteral("Ctrl+Alt+S");
    case HotkeyAction::PauseResume:  return QStringLiteral("Ctrl+Alt+P");
    case HotkeyAction::MicMute:      return QStringLiteral("Ctrl+Alt+M");
    case HotkeyAction::Screenshot:   return QStringLiteral("Ctrl+Alt+X");
    default:                         return {};
    }
}

QString hotkeyKey(HotkeyAction a)
{
    switch (a) {
    case HotkeyAction::RecordToggle: return QStringLiteral("record");
    case HotkeyAction::Stop:         return QStringLiteral("stop");
    case HotkeyAction::PauseResume:  return QStringLiteral("pause");
    case HotkeyAction::MicMute:      return QStringLiteral("micMute");
    case HotkeyAction::Screenshot:   return QStringLiteral("screenshot");
    default:                         return QStringLiteral("unknown");
    }
}

} // namespace

int AppSettings::crfForQuality() const
{
    switch (quality) {
    case QualityPreset::SmallFile:   return 28;
    case QualityPreset::Balanced:    return 23;
    case QualityPreset::HighQuality: return 19;
    case QualityPreset::Custom:      return std::clamp(crf, 0, 51);
    }
    return 23;
}

QString AppSettings::hotkeyName(HotkeyAction a)
{
    switch (a) {
    case HotkeyAction::RecordToggle: return QStringLiteral("Start / stop recording");
    case HotkeyAction::Stop:         return QStringLiteral("Stop recording");
    case HotkeyAction::PauseResume:  return QStringLiteral("Pause / resume");
    case HotkeyAction::MicMute:      return QStringLiteral("Mute / unmute microphone");
    case HotkeyAction::Screenshot:   return QStringLiteral("Take screenshot");
    default:                         return {};
    }
}

AppSettings loadSettings(const QString& iniPath)
{
    AppSettings s;
    QSettings q(iniPath, QSettings::IniFormat);
    // Default for a fresh install: the user's Videos folder (saved settings keep any chosen folder).
    QString videoRoot = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    videoRoot = videoRoot.isEmpty() ? QCoreApplication::applicationDirPath() + QStringLiteral("/Recordings")
                                    : videoRoot + QStringLiteral("/LumaCapture");

    q.beginGroup("general");
    s.outputDir = q.value("outputDir", videoRoot).toString();
    s.screenshotDir = q.value("screenshotDir", videoRoot + "/Screenshots").toString();
    s.namePattern = q.value("namePattern", s.namePattern).toString();
    s.container = static_cast<Container>(q.value("container", 0).toInt());
    s.keepMkvAfterMp4 = q.value("keepMkvAfterMp4", s.keepMkvAfterMp4).toBool();
    s.theme = static_cast<Theme>(q.value("theme", 0).toInt());
    s.countdownSeconds = q.value("countdown", 0).toInt();
    s.minimizeOnRecord = q.value("minimizeOnRecord", false).toBool();
    s.showRecordingBar = q.value("showRecordingBar", true).toBool();
    s.minimizeToTray = q.value("minimizeToTray", false).toBool();
    s.excludeOwnWindows = q.value("excludeOwnWindows", true).toBool();
    s.lowSpaceWarnMB = q.value("lowSpaceWarnMB", 2000).toInt();
    s.profile = q.value("profile", s.profile).toString();
    s.screenshotCursor = q.value("screenshotCursor", true).toBool();
    q.endGroup();

    q.beginGroup("source");
    s.sourceKind = static_cast<SourceKind>(std::clamp(q.value("kind", 0).toInt(), 0, 2));
    s.displayName = q.value("display").toString();
    s.windowTitle = q.value("windowTitle").toString();
    s.region = q.value("region", QRect(100, 100, 1280, 720)).toRect();
    s.captureCursor = q.value("captureCursor", true).toBool();
    s.cropLeft = q.value("cropLeft", 0).toInt();
    s.cropTop = q.value("cropTop", 0).toInt();
    s.cropRight = q.value("cropRight", 0).toInt();
    s.cropBottom = q.value("cropBottom", 0).toInt();
    s.cursor.highlight = q.value("highlight", false).toBool();
    s.cursor.highlightColor = colorFromString(q.value("highlightColor").toString(), s.cursor.highlightColor);
    s.cursor.highlightRadius = q.value("highlightRadius", s.cursor.highlightRadius).toFloat();
    s.cursor.clicks = q.value("clicks", false).toBool();
    s.cursor.leftClickColor = colorFromString(q.value("leftClickColor").toString(), s.cursor.leftClickColor);
    s.cursor.rightClickColor = colorFromString(q.value("rightClickColor").toString(), s.cursor.rightClickColor);
    s.cursor.clickRadius = q.value("clickRadius", s.cursor.clickRadius).toFloat();
    q.endGroup();

    q.beginGroup("video");
    s.resolution = static_cast<ResolutionPreset>(std::clamp(q.value("resolution", 0).toInt(), 0, 4));
    s.fps = q.value("fps", 30).toInt();
    if (s.fps != 24 && s.fps != 30 && s.fps != 60)
        s.fps = 30;
    s.quality = static_cast<QualityPreset>(std::clamp(q.value("quality", 1).toInt(), 0, 3));
    s.crf = q.value("crf", 23).toInt();
    s.x264Preset = q.value("x264Preset", s.x264Preset).toString();
    s.keyframeSeconds = q.value("keyframeSeconds", 2.0).toDouble();
    s.encoderThreads = q.value("threads", 3).toInt();
    s.cpuConvert = q.value("cpuConvert", false).toBool();
    s.queueCapacity = std::clamp(q.value("queue", 8).toInt(), 2, 64);
    auto& f = s.videoFilters;
    f.enabled = q.value("fxEnabled", false).toBool();
    f.brightness = q.value("fxBrightness", 0).toFloat();
    f.contrast = q.value("fxContrast", 1).toFloat();
    f.saturation = q.value("fxSaturation", 1).toFloat();
    f.gamma = q.value("fxGamma", 1).toFloat();
    f.temperature = q.value("fxTemperature", 0).toFloat();
    f.tint = q.value("fxTint", 0).toFloat();
    f.sharpen = q.value("fxSharpen", 0).toFloat();
    f.blur = q.value("fxBlur", 0).toFloat();
    f.grayscale = q.value("fxGrayscale", false).toBool();
    q.endGroup();

    q.beginGroup("audio");
    s.systemAudio = q.value("system", true).toBool();
    s.systemDevice = q.value("systemDevice").toString();
    s.systemVolume = q.value("systemVolume", 1.0).toDouble();
    s.microphone = q.value("mic", false).toBool();
    s.micDevice = q.value("micDevice").toString();
    s.micVolume = q.value("micVolume", 1.0).toDouble();
    s.micMuted = q.value("micMuted", false).toBool();
    s.micDelayMs = q.value("micDelayMs", 0).toInt();
    s.audioBitrate = q.value("bitrate", 160).toInt();
    s.separateTracks = q.value("separateTracks", false).toBool();
    auto& m = s.micFilters;
    m.enabled = q.value("fxEnabled", false).toBool();
    m.gainDb = q.value("gainDb", 0).toFloat();
    m.highPass = q.value("highPass", false).toBool();
    m.eq = q.value("eq", false).toBool();
    m.eqLowDb = q.value("eqLow", 0).toFloat();
    m.eqMidDb = q.value("eqMid", 0).toFloat();
    m.eqHighDb = q.value("eqHigh", 0).toFloat();
    m.gate = q.value("gate", false).toBool();
    m.gateThresholdDb = q.value("gateThreshold", -45).toFloat();
    m.gateReleaseMs = q.value("gateRelease", 150).toFloat();
    m.compressor = q.value("compressor", false).toBool();
    m.compThresholdDb = q.value("compThreshold", -20).toFloat();
    m.compRatio = q.value("compRatio", 3).toFloat();
    m.compMakeupDb = q.value("compMakeup", 3).toFloat();
    m.limiter = q.value("limiter", false).toBool();
    m.limiterCeilingDb = q.value("limiterCeiling", -1).toFloat();
    m.noiseSuppression = q.value("noiseSuppression", false).toBool();
    m.noiseReductionDb = q.value("noiseReduction", 12).toFloat();
    q.endGroup();

    q.beginGroup("webcam");
    s.webcamEnabled = q.value("enabled", false).toBool();
    s.webcamLink = q.value("link").toString();
    s.webcamName = q.value("name").toString();
    s.webcamMode.width = q.value("modeWidth", 0).toUInt();
    s.webcamMode.height = q.value("modeHeight", 0).toUInt();
    s.webcamMode.fpsNum = q.value("modeFpsNum", 30).toUInt();
    s.webcamMode.fpsDen = q.value("modeFpsDen", 1).toUInt();
    s.webcamMode.format = q.value("modeFormat").toString();
    s.webcamPreview = q.value("preview", true).toBool();
    auto& p = s.webcamPlacement;
    p.x = q.value("x", p.x).toFloat();
    p.y = q.value("y", p.y).toFloat();
    p.w = q.value("w", p.w).toFloat();
    p.cropLeft = q.value("cropLeft", 0).toFloat();
    p.cropTop = q.value("cropTop", 0).toFloat();
    p.cropRight = q.value("cropRight", 0).toFloat();
    p.cropBottom = q.value("cropBottom", 0).toFloat();
    p.mirror = q.value("mirror", false).toBool();
    p.shape = static_cast<gpu::WebcamShape>(std::clamp(q.value("shape", 1).toInt(), 0, 2));
    p.cornerRadius = q.value("cornerRadius", p.cornerRadius).toFloat();
    p.borderPx = q.value("border", 0).toFloat();
    p.borderColor = colorFromString(q.value("borderColor").toString(), p.borderColor);
    p.opacity = q.value("opacity", 1).toFloat();
    auto& w = s.webcamFilters;
    w.enabled = q.value("fxEnabled", true).toBool();
    w.brightness = q.value("fxBrightness", 0).toFloat();
    w.contrast = q.value("fxContrast", 1).toFloat();
    w.saturation = q.value("fxSaturation", 1).toFloat();
    w.gamma = q.value("fxGamma", 1).toFloat();
    w.exposure = q.value("fxExposure", 0).toFloat();
    w.temperature = q.value("fxTemperature", 0).toFloat();
    w.tint = q.value("fxTint", 0).toFloat();
    w.redGain = q.value("fxRed", 1).toFloat();
    w.greenGain = q.value("fxGreen", 1).toFloat();
    w.blueGain = q.value("fxBlue", 1).toFloat();
    w.sharpen = q.value("fxSharpen", 0).toFloat();
    w.denoise = q.value("fxDenoise", 0).toFloat();
    w.monochrome = q.value("fxMono", false).toBool();
    w.chromaKey = q.value("keyEnabled", false).toBool();
    w.keyColor = q.value("keyColor", 0x00FF00).toUInt();
    w.similarity = q.value("keySimilarity", 0.3).toFloat();
    w.smoothness = q.value("keySmoothness", 0.08).toFloat();
    w.spill = q.value("keySpill", 0.5).toFloat();
    s.webcamBackground = q.value("background").toString();
    q.endGroup();

    const int n = q.beginReadArray("overlays");
    for (int i = 0; i < n; ++i) {
        q.setArrayIndex(i);
        OverlayItem o;
        o.type = static_cast<OverlayItem::Type>(std::clamp(q.value("type", 0).toInt(), 0, 2));
        o.enabled = q.value("enabled", true).toBool();
        o.name = q.value("name").toString();
        o.x = q.value("x", o.x).toDouble();
        o.y = q.value("y", o.y).toDouble();
        o.w = q.value("w", o.w).toDouble();
        o.h = q.value("h", o.h).toDouble();
        o.opacity = q.value("opacity", 1.0).toDouble();
        o.text = q.value("text", o.text).toString();
        o.fontFamily = q.value("font", o.fontFamily).toString();
        o.fontSize = q.value("fontSize", o.fontSize).toInt();
        o.bold = q.value("bold", o.bold).toBool();
        o.color = QColor(q.value("color", "#ffffffff").toString());
        o.outline = q.value("outline", o.outline).toBool();
        o.outlineColor = QColor(q.value("outlineColor", "#ff000000").toString());
        o.outlineWidth = q.value("outlineWidth", o.outlineWidth).toInt();
        o.shadow = q.value("shadow", o.shadow).toBool();
        o.imagePath = q.value("image").toString();
        o.fillColor = QColor(q.value("fill", "#78000000").toString());
        o.borderWidth = q.value("borderWidth", 0).toInt();
        o.borderColor = QColor(q.value("borderColor", "#ffffffff").toString());
        o.cornerRadius = q.value("cornerRadius", 8).toInt();
        s.overlays.append(o);
    }
    q.endArray();

    q.beginGroup("watermark");
    s.watermark.enabled = q.value("enabled", false).toBool();
    s.watermark.text = q.value("text", s.watermark.text).toString();
    s.watermark.corner = q.value("corner", 3).toInt();
    s.watermark.opacity = q.value("opacity", 0.5).toDouble();
    s.watermark.fontSize = q.value("fontSize", 24).toInt();
    q.endGroup();

    q.beginGroup("hotkeys");
    for (int i = 0; i < static_cast<int>(HotkeyAction::Count); ++i) {
        const auto a = static_cast<HotkeyAction>(i);
        s.hotkeys[i] = QKeySequence(q.value(hotkeyKey(a), defaultHotkey(a)).toString(), QKeySequence::PortableText);
    }
    q.endGroup();
    return s;
}

void saveSettings(const AppSettings& s, const QString& iniPath)
{
    QSettings q(iniPath, QSettings::IniFormat);
    q.clear();

    q.beginGroup("general");
    q.setValue("outputDir", s.outputDir);
    q.setValue("screenshotDir", s.screenshotDir);
    q.setValue("namePattern", s.namePattern);
    q.setValue("container", static_cast<int>(s.container));
    q.setValue("keepMkvAfterMp4", s.keepMkvAfterMp4);
    q.setValue("theme", static_cast<int>(s.theme));
    q.setValue("countdown", s.countdownSeconds);
    q.setValue("minimizeOnRecord", s.minimizeOnRecord);
    q.setValue("showRecordingBar", s.showRecordingBar);
    q.setValue("minimizeToTray", s.minimizeToTray);
    q.setValue("excludeOwnWindows", s.excludeOwnWindows);
    q.setValue("lowSpaceWarnMB", s.lowSpaceWarnMB);
    q.setValue("profile", s.profile);
    q.setValue("screenshotCursor", s.screenshotCursor);
    q.endGroup();

    q.beginGroup("source");
    q.setValue("kind", static_cast<int>(s.sourceKind));
    q.setValue("display", s.displayName);
    q.setValue("windowTitle", s.windowTitle);
    q.setValue("region", s.region);
    q.setValue("captureCursor", s.captureCursor);
    q.setValue("cropLeft", s.cropLeft);
    q.setValue("cropTop", s.cropTop);
    q.setValue("cropRight", s.cropRight);
    q.setValue("cropBottom", s.cropBottom);
    q.setValue("highlight", s.cursor.highlight);
    q.setValue("highlightColor", colorToString(s.cursor.highlightColor));
    q.setValue("highlightRadius", s.cursor.highlightRadius);
    q.setValue("clicks", s.cursor.clicks);
    q.setValue("leftClickColor", colorToString(s.cursor.leftClickColor));
    q.setValue("rightClickColor", colorToString(s.cursor.rightClickColor));
    q.setValue("clickRadius", s.cursor.clickRadius);
    q.endGroup();

    q.beginGroup("video");
    q.setValue("resolution", static_cast<int>(s.resolution));
    q.setValue("fps", s.fps);
    q.setValue("quality", static_cast<int>(s.quality));
    q.setValue("crf", s.crf);
    q.setValue("x264Preset", s.x264Preset);
    q.setValue("keyframeSeconds", s.keyframeSeconds);
    q.setValue("threads", s.encoderThreads);
    q.setValue("cpuConvert", s.cpuConvert);
    q.setValue("queue", s.queueCapacity);
    const auto& f = s.videoFilters;
    q.setValue("fxEnabled", f.enabled);
    q.setValue("fxBrightness", f.brightness);
    q.setValue("fxContrast", f.contrast);
    q.setValue("fxSaturation", f.saturation);
    q.setValue("fxGamma", f.gamma);
    q.setValue("fxTemperature", f.temperature);
    q.setValue("fxTint", f.tint);
    q.setValue("fxSharpen", f.sharpen);
    q.setValue("fxBlur", f.blur);
    q.setValue("fxGrayscale", f.grayscale);
    q.endGroup();

    q.beginGroup("audio");
    q.setValue("system", s.systemAudio);
    q.setValue("systemDevice", s.systemDevice);
    q.setValue("systemVolume", s.systemVolume);
    q.setValue("mic", s.microphone);
    q.setValue("micDevice", s.micDevice);
    q.setValue("micVolume", s.micVolume);
    q.setValue("micMuted", s.micMuted);
    q.setValue("micDelayMs", s.micDelayMs);
    q.setValue("bitrate", s.audioBitrate);
    q.setValue("separateTracks", s.separateTracks);
    const auto& m = s.micFilters;
    q.setValue("fxEnabled", m.enabled);
    q.setValue("gainDb", m.gainDb);
    q.setValue("highPass", m.highPass);
    q.setValue("eq", m.eq);
    q.setValue("eqLow", m.eqLowDb);
    q.setValue("eqMid", m.eqMidDb);
    q.setValue("eqHigh", m.eqHighDb);
    q.setValue("gate", m.gate);
    q.setValue("gateThreshold", m.gateThresholdDb);
    q.setValue("gateRelease", m.gateReleaseMs);
    q.setValue("compressor", m.compressor);
    q.setValue("compThreshold", m.compThresholdDb);
    q.setValue("compRatio", m.compRatio);
    q.setValue("compMakeup", m.compMakeupDb);
    q.setValue("limiter", m.limiter);
    q.setValue("limiterCeiling", m.limiterCeilingDb);
    q.setValue("noiseSuppression", m.noiseSuppression);
    q.setValue("noiseReduction", m.noiseReductionDb);
    q.endGroup();

    q.beginGroup("webcam");
    q.setValue("enabled", s.webcamEnabled);
    q.setValue("link", s.webcamLink);
    q.setValue("name", s.webcamName);
    q.setValue("modeWidth", s.webcamMode.width);
    q.setValue("modeHeight", s.webcamMode.height);
    q.setValue("modeFpsNum", s.webcamMode.fpsNum);
    q.setValue("modeFpsDen", s.webcamMode.fpsDen);
    q.setValue("modeFormat", s.webcamMode.format);
    q.setValue("preview", s.webcamPreview);
    const auto& p = s.webcamPlacement;
    q.setValue("x", p.x);
    q.setValue("y", p.y);
    q.setValue("w", p.w);
    q.setValue("cropLeft", p.cropLeft);
    q.setValue("cropTop", p.cropTop);
    q.setValue("cropRight", p.cropRight);
    q.setValue("cropBottom", p.cropBottom);
    q.setValue("mirror", p.mirror);
    q.setValue("shape", static_cast<int>(p.shape));
    q.setValue("cornerRadius", p.cornerRadius);
    q.setValue("border", p.borderPx);
    q.setValue("borderColor", colorToString(p.borderColor));
    q.setValue("opacity", p.opacity);
    const auto& w = s.webcamFilters;
    q.setValue("fxEnabled", w.enabled);
    q.setValue("fxBrightness", w.brightness);
    q.setValue("fxContrast", w.contrast);
    q.setValue("fxSaturation", w.saturation);
    q.setValue("fxGamma", w.gamma);
    q.setValue("fxExposure", w.exposure);
    q.setValue("fxTemperature", w.temperature);
    q.setValue("fxTint", w.tint);
    q.setValue("fxRed", w.redGain);
    q.setValue("fxGreen", w.greenGain);
    q.setValue("fxBlue", w.blueGain);
    q.setValue("fxSharpen", w.sharpen);
    q.setValue("fxDenoise", w.denoise);
    q.setValue("fxMono", w.monochrome);
    q.setValue("keyEnabled", w.chromaKey);
    q.setValue("keyColor", w.keyColor);
    q.setValue("keySimilarity", w.similarity);
    q.setValue("keySmoothness", w.smoothness);
    q.setValue("keySpill", w.spill);
    q.setValue("background", s.webcamBackground);
    q.endGroup();

    q.beginWriteArray("overlays", static_cast<int>(s.overlays.size()));
    for (int i = 0; i < s.overlays.size(); ++i) {
        q.setArrayIndex(i);
        const OverlayItem& o = s.overlays[i];
        q.setValue("type", static_cast<int>(o.type));
        q.setValue("enabled", o.enabled);
        q.setValue("name", o.name);
        q.setValue("x", o.x);
        q.setValue("y", o.y);
        q.setValue("w", o.w);
        q.setValue("h", o.h);
        q.setValue("opacity", o.opacity);
        q.setValue("text", o.text);
        q.setValue("font", o.fontFamily);
        q.setValue("fontSize", o.fontSize);
        q.setValue("bold", o.bold);
        q.setValue("color", o.color.name(QColor::HexArgb));
        q.setValue("outline", o.outline);
        q.setValue("outlineColor", o.outlineColor.name(QColor::HexArgb));
        q.setValue("outlineWidth", o.outlineWidth);
        q.setValue("shadow", o.shadow);
        q.setValue("image", o.imagePath);
        q.setValue("fill", o.fillColor.name(QColor::HexArgb));
        q.setValue("borderWidth", o.borderWidth);
        q.setValue("borderColor", o.borderColor.name(QColor::HexArgb));
        q.setValue("cornerRadius", o.cornerRadius);
    }
    q.endArray();

    q.beginGroup("watermark");
    q.setValue("enabled", s.watermark.enabled);
    q.setValue("text", s.watermark.text);
    q.setValue("corner", s.watermark.corner);
    q.setValue("opacity", s.watermark.opacity);
    q.setValue("fontSize", s.watermark.fontSize);
    q.endGroup();

    q.beginGroup("hotkeys");
    for (int i = 0; i < static_cast<int>(HotkeyAction::Count); ++i)
        q.setValue(hotkeyKey(static_cast<HotkeyAction>(i)), s.hotkeys[i].toString(QKeySequence::PortableText));
    q.endGroup();
    q.sync();
}

QStringList profileNames()
{
    return {QStringLiteral("Low Resource"), QStringLiteral("Balanced"), QStringLiteral("High Quality"),
            QStringLiteral("Webcam Tutorial"), QStringLiteral("Screen Only")};
}

QString profileDescription(const QString& name)
{
    if (name == "Low Resource")
        return QStringLiteral("720p at 30 FPS, fastest encoder. Lightest load; best when the CPU runs hot.");
    if (name == "Balanced")
        return QStringLiteral("Native resolution at 30 FPS, fastest encoder, balanced quality. Recommended default.");
    if (name == "High Quality")
        return QStringLiteral("Native resolution at 30 FPS with a slower encoder preset and higher quality. "
                              "Uses much more CPU; may drop frames on an i5-2400S under load or when it throttles.");
    if (name == "Webcam Tutorial")
        return QStringLiteral("Screen + round webcam in the corner, microphone with voice processing, "
                              "cursor highlight and click rings.");
    if (name == "Screen Only")
        return QStringLiteral("Screen and system audio only; webcam and microphone off.");
    return {};
}

void applyProfile(AppSettings& s, const QString& name)
{
    s.profile = name;
    if (name == "Low Resource") {
        s.resolution = ResolutionPreset::P720;
        s.fps = 30;
        s.quality = QualityPreset::Balanced;
        s.x264Preset = QStringLiteral("ultrafast");
        s.webcamFilters.sharpen = 0;
        s.webcamFilters.denoise = 0;
        s.micFilters.noiseSuppression = false;
        s.videoFilters.enabled = false;
    } else if (name == "Balanced") {
        s.resolution = ResolutionPreset::Native;
        s.fps = 30;
        s.quality = QualityPreset::Balanced;
        s.x264Preset = QStringLiteral("ultrafast");
    } else if (name == "High Quality") {
        s.resolution = ResolutionPreset::Native;
        s.fps = 30;
        s.quality = QualityPreset::HighQuality;
        s.x264Preset = QStringLiteral("superfast");
    } else if (name == "Webcam Tutorial") {
        s.resolution = ResolutionPreset::Native;
        s.fps = 30;
        s.quality = QualityPreset::Balanced;
        s.x264Preset = QStringLiteral("ultrafast");
        s.webcamEnabled = true;
        s.webcamPlacement.shape = gpu::WebcamShape::Circle;
        s.webcamPlacement.w = 0.18f;
        s.webcamPlacement.x = 0.80f;
        s.webcamPlacement.y = 0.64f;
        s.webcamPlacement.borderPx = 4;
        s.microphone = true;
        s.systemAudio = true;
        s.micFilters.enabled = true;
        s.micFilters.highPass = true;
        s.micFilters.gate = true;
        s.micFilters.compressor = true;
        s.micFilters.limiter = true;
        s.cursor.highlight = true;
        s.cursor.clicks = true;
    } else if (name == "Screen Only") {
        s.webcamEnabled = false;
        s.microphone = false;
        s.systemAudio = true;
    }
}

QStringList webcamFilterPresetNames()
{
    return {QStringLiteral("Natural"), QStringLiteral("Brighter"), QStringLiteral("Warm"), QStringLiteral("Cool"),
            QStringLiteral("Monochrome"), QStringLiteral("Low light")};
}

void applyWebcamFilterPreset(webcam::WebcamFilterSettings& f, const QString& name)
{
    // Keep the chroma key configuration; reset the image adjustments.
    const auto key = f.chromaKey;
    const auto keyColor = f.keyColor;
    const auto sim = f.similarity, smooth = f.smoothness, spill = f.spill;
    const auto bg = f.background;
    f = webcam::WebcamFilterSettings{};
    f.chromaKey = key;
    f.keyColor = keyColor;
    f.similarity = sim;
    f.smoothness = smooth;
    f.spill = spill;
    f.background = bg;
    if (name == "Brighter") {
        f.brightness = 0.05f;
        f.gamma = 1.25f;
        f.contrast = 1.05f;
    } else if (name == "Warm") {
        f.temperature = 0.35f;
        f.saturation = 1.1f;
    } else if (name == "Cool") {
        f.temperature = -0.35f;
    } else if (name == "Monochrome") {
        f.monochrome = true;
        f.contrast = 1.1f;
    } else if (name == "Low light") {
        // Lift shadows with gamma rather than brightness (keeps blacks), slight exposure,
        // mild denoise to fight sensor noise, gentle sharpening. Adjust to taste.
        f.exposure = 0.4f;
        f.gamma = 1.4f;
        f.contrast = 1.08f;
        f.saturation = 1.1f;
        f.denoise = 0.5f;
        f.sharpen = 0.2f;
    }
}

} // namespace luma::app
