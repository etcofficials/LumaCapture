# Changelog

## 1.0.1 - stabilisation release

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

## 1.0.0

First complete application.
