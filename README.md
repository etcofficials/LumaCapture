# LumaCapture

A lightweight, recording-focused screen recorder for older Windows PCs.
Built and tuned on an Intel Core i5-2400S with a GeForce GT 730 (Fermi, no NVENC)
running Windows 10 22H2. No streaming, no scenes, no accounts, no network access.

**Version 1.0.0** - see [CHANGELOG.md](CHANGELOG.md).
**Download:** [Releases](https://github.com/etcofficials/LumaCapture/releases) (portable ZIP, Windows 10/11 x64).

## Features

- **Capture sources:** a whole display (Desktop Duplication), a single window
  (Windows.Graphics.Capture), or a screen region, with cursor capture, cursor
  highlight and left/right click rings.
- **GPU compositing:** one Direct3D 11 pass does crop/scale, colour filters,
  cursor effects, the webcam overlay and text/image overlays, then converts to
  NV12 for the encoder.
- **Encoding:** H.264 with libx264 (software; there is no hardware encoder on
  the target GPU) and AAC audio. Recordings are MKV (crash-tolerant), with
  optional MP4 conversion after stopping.
- **Audio:** system audio (WASAPI loopback), microphone, or both.
  - Separate tracks are optional.
  - The microphone chain has gain, high-pass, 3-band EQ, noise gate,
    compressor, limiter, FFT noise suppression (FFmpeg `afftdn`) and a sync
    delay.
- **Webcam:** Media Foundation capture with the camera's real modes and
  hardware controls. It includes:
  - **Auto adjust**, presets, before/after comparison, and brightness,
    contrast, gamma, exposure, white balance, RGB, sharpen and low-light
    denoise filters.
  - **Anti-flicker** (50/60 Hz) and a **low-light frame-rate** choice
    ("smooth motion" or "brighter").
  - Green screen with a background image.
  - Rectangle/rounded/circle overlay with border, crop, mirror and opacity.
- **Workflow:**
  - Explicit recording states: Ready, Starting, Recording, Paused, Saving,
    Error. Pause/resume keeps audio and video in sync.
  - Global hotkeys with conflict detection, PNG screenshots, a countdown, a
    floating recording bar (hidden from the recording) and a tray icon.
  - Recording history, free-space checks, and crash recovery that repairs
    interrupted files.
- **Diagnostics:**
  - Log files, crash minidumps and UI-hang dumps are written next to the app
    and never sent anywhere.
  - A scripted self-test and a small test suite are included.

## Download / run (portable)

The portable build lives in `dist\LumaCapture\`. Double-click
**`Launch-LumaCapture.bat`** (or `LumaCapture.exe`). Nothing is installed.
`Create-Desktop-Shortcut.bat` optionally adds a desktop shortcut after asking.

Settings, history, logs and crash dumps: `LumaCapture-data\` next to the exe.
Recordings: your Windows **Videos\LumaCapture** folder by default (change it in the app).

## Build

The toolchain lives entirely on G::

| Component | Location |
|---|---|
| MSVC 14.44 + Windows SDK 10.0.26100 (portable) | `G:\DevTools` (`setup-msvc.ps1`) |
| CMake 4.x + Ninja | `G:\DevTools` |
| Qt 6.8.3 msvc2022_64 | `G:\Qt` |
| FFmpeg 7.1.5 shared, GPL build with libx264 | `G:\DevTools\ffmpeg` |

```
powershell -File scripts\build.ps1              # Release build -> build\release\bin
powershell -File scripts\package.ps1            # build + portable folder + verification -> dist\LumaCapture
powershell -File scripts\test.ps1 [-Hardware] [-Gui]
```

`build.ps1` sets up the MSVC environment for the current process only,
redirects TEMP/TMP to `build\tmp`, and uses the `release` CMake preset (Ninja).

## Architecture

```
src/core   luma_core static library (no Qt)
  capture/   CaptureSource, DesktopDuplicator, WindowCapture (WGC), Screenshot, PointerShape
  gpu/       D3D11Device, Compositor + shaders/Compose.hlsl, ReadbackRing, CursorOverlay
  webcam/    WebcamCapture (async Media Foundation reader), WebcamFilters (auto adjust, LUT filters), FrameExchange
  audio/     WasapiSource, AudioRing, MicProcessor, AudioEngine (mixer), AudioDevices
  encode/    VideoEncoder (libx264), AudioEncoder (AAC), FramePool
  mux/       Muxer (MKV, 1 s flush), remux (MP4 export / crash repair), probeMedia
  session/   RecordingSession, CaptureLoop, EncodeLoop, RecordingState, PauseTimeline, BoundedQueue
src/app    Qt 6 Widgets GUI (controllers, dialogs, widgets, crash handler)
src/tests  luma-tests: unit tests + opt-in hardware integration tests
src/tools  luma-bench: pipeline benchmark
```

Threads:
- **Capture:** source → GPU compositor → staging readback ring → frame pool →
  bounded queue. It never waits on the encoder; when the queue is full, the
  newest frame is dropped.
- **Encode:** queue → libx264 → muxer.
- **Audio:** WASAPI devices → rings positioned by QPC timestamp → mixer →
  AAC → muxer.
- **Webcam:** asynchronous source reader → filters → latest-frame exchange.
- **UI:** Qt only. Device work, start, stop, finalizing, remux and recovery
  run on workers.

## Known limitations

- There is no hardware video encoder on the GT 730 (Fermi). Video is encoded
  by x264 on the CPU. 1080p60 and slower presets at 1080p could not keep up in
  tests on the i5-2400S, and the test PC throttles under sustained load.
- On Windows 10, Windows draws a yellow border around a window captured with
  WGC. A window that is closed while recording stays on its last image.
- Region capture must be inside one monitor.
- There is no acoustic echo cancellation and no AI background removal/blur:
  only chroma key, which needs a green screen.
- A webcam in dim light is limited by its sensor. See
  [docs/USER-GUIDE.txt](docs/USER-GUIDE.txt), "Webcam quality".
- A crash loses at most about the last second of a recording. A power cut can
  lose what Windows had not yet written to the disk.

## Testing

See [docs/TESTING.md](docs/TESTING.md) for what was tested, how, and the results.

## License

LumaCapture is distributed under the **GNU General Public License v3.0 or
later** ([LICENSE](LICENSE)). It links against a GPL-3.0 FFmpeg build that
contains libx264. Qt is used under the LGPL-3.0 (dynamically linked). See
[docs/THIRD-PARTY-NOTICES.txt](docs/THIRD-PARTY-NOTICES.txt).
