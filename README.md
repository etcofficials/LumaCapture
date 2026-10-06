# LumaCapture

A lightweight, recording-focused screen recorder for older Windows PCs.
Built and tuned on an Intel Core i5-2400S with a GeForce GT 730 (Fermi, no hardware
encoder) running Windows 10 22H2. No streaming, no scenes, no accounts, no network access.

**Version 2.0.0** (in development - see [CHANGELOG.md](CHANGELOG.md) and
[docs/TESTING.md](docs/TESTING.md) for what is verified).
**Download:** [Releases](https://github.com/etcofficials/LumaCapture/releases) -
portable ZIP or installer, Windows 10/11 x64.

> Open it → choose what to record → press Record → get a good recording.

## Features

- **Capture modes:** Display, Window (Windows.Graphics.Capture), Region, and Game (Beta:
  records the monitor a game is on). Cursor capture, cursor highlight, click rings.
- **Live preview** of exactly what will be recorded (rendered on the GPU from the
  encoded frames, low frame rate, paused when the window is minimised).
- **Video:** H.264 with x264 on the CPU (the target GPU has no hardware encoder), AAC
  audio, crash-safe MKV with optional MP4 conversion. 24/25/30/50/60 FPS; Low /
  Balanced / High / Very High quality plus Advanced (CRF, preset, keyframes, threads);
  a per-setting performance estimate and a "falling behind" warning.
- **Audio:** system audio and microphone with device selection, volume, mute, sync
  delay, 25 Hz level meters; optional voice processing (noise suppression, gate,
  compressor, limiter, EQ); optional separate tracks.
- **Webcam:** real camera modes and hardware controls (only those the camera reports),
  Auto adjust, presets, before/after, brightness/contrast/saturation/gamma/sharpness,
  anti-flicker 50/60 Hz, low-light "smooth motion" vs "brighter image", green screen.
- **Overlays:** webcam (position, size, opacity, shape, corner radius, border, mirror),
  text, images, watermark, and a drag-and-drop layout editor.
- **Library:** thumbnails, duration, resolution, FPS, size, date; search and sort; play,
  open with, show in folder, rename, details (actual codec / pixel format / frame
  rate), delete to the Recycle Bin; import and drag & drop of existing videos.
- **Workflow:** global hotkeys, screenshots (PNG, without LumaCapture's own windows),
  countdown, tray icon, optional recording HUD (off by default), crash recovery.
- **Own windows are kept out of recordings** (WDA_EXCLUDEFROMCAPTURE applied before a
  window is first shown; if Windows refuses, LumaCapture minimises itself while
  recording a screen).
- **Diagnostics:** optional performance panel (FPS, drops, CPU, RAM, GPU, latencies,
  queue, bitrate), logs and crash/hang dumps - all local, never uploaded.

## Install / run

- **Installer:** `LumaCapture-v2.0.0-Setup.exe` - per-user (no admin rights), Start Menu
  shortcut, optional Desktop shortcut, uninstaller in *Apps & features*.
- **Portable:** extract `LumaCapture-v2.0.0-Portable-Windows-x64.zip` and run
  `LumaCapture.exe`.

Settings, library list, logs and crash dumps are stored in `LumaCapture-data` next to
the exe when that folder is writable (portable use, or an install into a user folder),
otherwise in `%LOCALAPPDATA%\LumaCapture`. Recordings go to `Videos\LumaCapture` by
default.

## Build

The toolchain lives entirely on G::

| Component | Location |
|---|---|
| MSVC 14.44 + Windows SDK 10.0.26100 (portable) | `G:\DevTools` (`setup-msvc.ps1`) |
| CMake 4.x + Ninja | `G:\DevTools` |
| Qt 6.8.3 msvc2022_64 | `G:\Qt` |
| FFmpeg 7.1.5 shared, GPL build with libx264 | `G:\DevTools\ffmpeg` |
| NSIS 3.11 (installer only) | `G:\DevTools\nsis` |

```
powershell -File scripts\build.ps1                          # Release build -> build\release\bin
powershell -File scripts\package.ps1                        # portable folder + checks -> dist\LumaCapture
powershell -File scripts\make-release.ps1 -Version 2.0.0    # ZIP + Setup.exe + SHA256SUMS -> release\
powershell -File scripts\test.ps1 [-Hardware] [-Gui]        # tests (short, controlled)
python scripts\make-icon.py                                 # regenerates resources\lumacapture.ico
```

## Architecture

```
src/core   luma_core static library (no Qt)
  capture/   DesktopDuplicator (DXGI), WindowCapture (WGC), Screenshot, PointerShape
  gpu/       D3D11Device, Compositor + shaders/Compose.hlsl (NV12 + preview), ReadbackRing, CursorOverlay
  webcam/    WebcamCapture (async Media Foundation reader), WebcamFilters, FrameExchange
  audio/     WasapiSource, AudioRing, MicProcessor, AudioEngine
  encode/    VideoEncoder (libx264), AudioEncoder (AAC), FramePool
  mux/       Muxer (MKV via a background file writer), remux, probeMedia/probeDetails
  session/   RecordingSession (also preview-only), CaptureLoop, EncodeLoop, PreviewExchange,
             RecordingState, PauseTimeline, BoundedQueue
  util/      asynchronous Log, QPC clock, process/GPU metrics
src/app    Qt 6 Widgets GUI
  MainWindow, SettingsStore + DiskWorker (no disk I/O on the UI thread), RecordingController,
  PreviewController, WebcamController, ThumbnailCache, MediaImporter, History
  ui/        RecordPage, LibraryPage, SettingsPage, AboutPage, context panels, WebcamPanel
src/tests  luma-tests: unit tests + opt-in hardware tests (incl. capture-exclusion probe)
src/tools  luma-bench: pipeline benchmark
installer/ NSIS script
```

Threads: capture (source → GPU compositor → staging readback → bounded queue; never
waits for the encoder, drops the newest frame when the queue is full), encode (x264),
file writer (the muxer's output), audio (WASAPI → rings → mixer → AAC), webcam (async
reader → filters → latest-frame exchange), disk worker (settings/history), log writer,
thumbnail worker. The UI thread only copies small images and updates widgets.

## Known limitations

- No hardware video encoder on the GT 730 (Fermi): video is encoded by x264 on the CPU.
  1080p60 is likely to drop frames on a 4-core Sandy Bridge, especially when it throttles.
- Windows 10 draws a yellow border around a window captured with WGC.
- Region capture must be inside one monitor. Multi-monitor setups were not available
  for testing.
- No acoustic echo cancellation, no AI background removal (chroma key only).
- A webcam in dim light is limited by its sensor (see the user guide).

## License

GNU General Public License v3.0 or later ([LICENSE](LICENSE)). LumaCapture links against
a GPL-3.0 FFmpeg build that contains libx264; Qt is used under the LGPL-3.0 (dynamically
linked). See [docs/THIRD-PARTY-NOTICES.txt](docs/THIRD-PARTY-NOTICES.txt).

Feedback: Instagram [@etcofficials](https://www.instagram.com/etcofficials) ·
etcofficials28@gmail.com · [issues](https://github.com/etcofficials/LumaCapture/issues)
