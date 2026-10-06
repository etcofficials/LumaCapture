# Changelog

## 2.0.0 - major engineering pass (in development; not yet built or tested)

Written during a period when the development PC's G: drive was failing, so this
version has **not been compiled or tested yet**. See docs/TESTING.md.

### Root causes found in 1.0
- **UI hangs / freezes:** settings and history were saved on the UI thread with a forced
  disk flush (`QSettings::sync` -> `QLockFile` -> `FlushFileBuffers`, `QSaveFile`). On a
  slow or failing HDD that blocked the window for tens of seconds (confirmed with a hang
  dump: the UI thread was stuck in `FlushFileBuffers` while closing). Every log line was
  also written synchronously under a lock shared by all threads.
- **Recording stalls on a slow disk:** the encoder thread wrote the file itself, so a disk
  stall (HDD spin-up, sector retries) stopped encoding and caused dropped frames.
- **Image quality:** the default x264 "ultrafast" preset switches deblocking off, which
  shows block edges in moving content; downscaling (e.g. 1080p -> 720p) used one bilinear
  sample per pixel, which aliases text. Measured on this PC (see TESTING.md): slower
  presets cost 40-80 % more CPU and scored *lower* at the same CRF - quality is best
  bought with ultrafast + deblocking + a lower CRF.
- **UI load while recording:** the webcam preview repainted at 30 Hz through an
  antialiased clip path (Qt's slowest software path); meters updated only at 4 Hz.

### Changed / fixed
- No disk I/O on the UI thread: settings and history are written by a background disk
  worker (newest snapshot wins); asynchronous, rate-limited logger.
- The recording file is written by a dedicated writer thread (bounded 64 MB buffer).
- Quality presets Low / Balanced / High / Very High (x264 ultrafast + deblocking, CRF
  26 / 21 / 18 / 16) plus Advanced; frame rates 24/25/30/50/60; performance estimate and
  resolution guide; "recording is falling behind" warning.
- Four-tap area-approximating downscale in the GPU compositor.
- LumaCapture's own windows are excluded from capture *before* they are first shown; if
  Windows refuses, the main window is minimised while recording a screen. The recording
  HUD is off by default and is no longer a layered (translucent) window.
- Screenshots never include LumaCapture's windows (same exclusion).

### New
- Completely redesigned UI: Record / Library / Settings / Help-About pages; capture mode
  list (Display, Window, Region, Game Beta), sources, output, quick settings; live
  preview; transport controls; 25 Hz meters; recent recordings; context tabs (Video,
  Audio, Webcam, Effects, Overlays); optional performance panel.
- Library with cached thumbnails, search, sort, rename, details (actual media
  properties), Recycle Bin delete, import and drag & drop.
- Overlay editing in the Overlays tab (webcam placement, text, images, watermark).
- About page with feedback links, bug reports and "check for updates" (opens the browser).
- Installer (NSIS, per-user) with Start Menu / optional Desktop shortcut and uninstaller;
  portable ZIP; new app icon.
- Data folder falls back to %LOCALAPPDATA%\LumaCapture when the program folder is not
  writable (e.g. Program Files).

## 1.0.0 - first public release (2026-09-27)

The changes below are relative to the unreleased internal build that preceded it.

### Fixed

- **UI freezes: the app stopped responding when the webcam was turned off, the
  webcam settings page was opened, or a recording finished.**
  - Windows reported these as "cross-thread hangs".
  - Root cause: `WebcamCapture::stop()` shut the Media Foundation source down
    from the UI thread, then joined a camera thread that was blocked in a
    *synchronous* `IMFSourceReader::ReadSample`. That call never returns once
    the source is shut down from another thread (confirmed with debugger
    stacks).
  - The camera now uses an asynchronous source reader, and its thread shuts the
    device down itself.
  - Camera stop is non-blocking: stopped cameras are "retired" and released in
    the background.
- **Crash-safety: an interrupted MKV could lose everything recorded so far.**
  - The Matroska muxer kept up to 5 s of data in memory, plus a 32 KB file
    buffer.
  - Clusters are now closed and the file is flushed every second.
  - Verified: a recording killed after 3 s is repaired into a decodable file.
- **Exceptions escaping worker threads would terminate the process.** All
  thread entry points now catch everything and report the error.
- **A partially failed recording start could leave a joinable thread behind**
  (process termination). The start path now cleans up everything that was
  already running.
- `RecordingSession::stop()` is serialised and idempotent, so it is safe when
  stop is requested from several places.
- Audio-meter devices are released off the UI thread.
- Recording state is an explicit state machine (Idle, Starting, Recording,
  Paused, Stopping, Finalizing, Error). Double start/stop, pause while not
  recording and similar requests are rejected. "Saved" is reported only after
  the file is finalised and read back.

### Webcam

- **Auto adjust.** One-shot analysis sets exposure/gamma, contrast and white
  balance, with light denoise in dim scenes. It is deterministic, so the image
  does not pump.
- **Before/after preview**, preset menu, reset, and a filters on/off switch.
- **Camera & lighting controls:** anti-flicker (50/60 Hz) and the low-light
  frame-rate priority choice, alongside the camera's own controls.
- A live frame-pacing readout explains stutter caused by long exposures in dim
  light.
- Preview at the camera frame rate (was 15 fps). Only the latest frame is ever
  shown.
- The filter chain is about 4× faster in the common cases (1.1 ms instead of
  4.5 ms per 720p frame).

### Interface

- Redesigned main window:
  - A recording panel with a state indicator, timer, and live frame rate,
    dropped frames and file size.
  - Clear Start / Pause / Stop controls, a "what will be recorded" summary and
    notification banners instead of pop-ups.
  - Readable two-line history and a status bar with the recording folder and
    free space.
- Consistent theme (dark and light): control sizes, radii, focus rings, toggle
  switches and dropdown/spin arrows.
- Crash dumps and UI-hang dumps (`LumaCapture-data\crashdumps`) for diagnosis.

### Release

- `Launch-LumaCapture.bat` launcher and optional `Create-Desktop-Shortcut.bat`.
- The packaging script verifies DLL dependencies and a standalone launch from a
  path with spaces, and preserves the user's `LumaCapture-data`.
- Test suite: `luma-tests` (unit + hardware) and a scripted GUI self-test.
- The default recordings folder is Windows **Videos\LumaCapture** (it was a
  fixed drive path).
