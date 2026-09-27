LumaCapture 1.0.0 - portable screen recorder for Windows (x64)
================================================================

LumaCapture is a lightweight screen recorder made for older PCs. It records
the screen, a window or a region, with system audio, microphone and an
optional webcam overlay. It has no accounts, no telemetry and no internet
access.

Project page and source code: https://github.com/etcofficials/LumaCapture

INSTALL (no installer - portable)
  1. Extract this whole ZIP: right-click it > "Extract All...". Do not run it
     from inside the ZIP.
  2. Put the "LumaCapture" folder anywhere you like, for example
     D:\Apps\LumaCapture. Paths with spaces are fine; no admin rights are
     needed.
  3. Open the folder and double-click "Launch-LumaCapture.bat" (or
     LumaCapture.exe).
  4. Optional: run "Create-Desktop-Shortcut.bat" to add a desktop shortcut.
     It asks first.

  If Windows SmartScreen says "Windows protected your PC", click
  "More info" > "Run anyway". The app is not code-signed.

  To uninstall, delete the LumaCapture folder. Nothing else is written to
  the system except your recordings.

FIRST RECORDING
  1. Choose what to record: Display, Window or Region.
  2. Turn System audio and/or Microphone on (the meters show the levels).
  3. Optional: switch the Webcam on. "Image & camera..." adjusts the picture
     (Auto adjust, presets, anti-flicker); "Layout & overlays..." places it.
  4. Press "Start recording" (or Ctrl+Alt+R). Press it again to stop.
  5. Recordings are saved to your Videos\LumaCapture folder as .mkv files
     (MP4 is optional in Settings). Change the folder with "Change...".

  Hotkeys: Ctrl+Alt+R start/stop, Ctrl+Alt+P pause/resume, Ctrl+Alt+M mute
  mic, Ctrl+Alt+X screenshot.
  The full guide is LumaCapture\README.txt.

SYSTEM REQUIREMENTS
  - Windows 10 (version 1809 or later) or Windows 11, 64-bit.
  - A DirectX 11 graphics card. No hardware video encoder is needed:
    LumaCapture uses x264 on the CPU.
  - About 200 MB of disk space for the program, plus space for recordings.

HARDWARE LIMITATIONS - PLEASE READ
  - Video is encoded on the CPU, so the CPU decides what is possible.
    The recommended start is Native/1080p, 30 FPS, encoder speed
    "ultrafast", quality "Balanced". On an older quad-core (tested on an
    Intel i5-2400S) 1080p30 and 720p60 at "ultrafast" keep up; 1080p60 and
    slower presets at 1080p do not.
  - Watch the "Dropped frames" value while recording. If it rises, lower
    the frame rate or resolution.
  - Laptops and small PCs that overheat will slow down during long
    recordings.
  - Webcams in dim rooms may stutter because the sensor exposes longer than
    one frame. Add light, or use "Image & camera..." > Camera & lighting >
    Low light: "Smooth motion", plus Auto adjust.
  - Window capture on Windows 10 shows a yellow border drawn by Windows.
  - There is no echo cancellation: use headphones with a microphone.

IF SOMETHING GOES WRONG
  - If LumaCapture is closed unexpectedly while recording, it offers to
    recover the recording the next time it starts.
  - Logs and crash reports are kept locally in LumaCapture\LumaCapture-data
    and are never uploaded. Please attach the newest log when reporting a
    problem: https://github.com/etcofficials/LumaCapture/issues

LICENSE
  LumaCapture is free software under the GNU General Public License v3.0
  (LumaCapture\licenses\GPL-3.0.txt). It includes FFmpeg (GPL-3.0 build with
  x264) and Qt 6 (LGPL-3.0). See LumaCapture\licenses\THIRD-PARTY-NOTICES.txt
  for the components, their licenses and where to get their source code.
  The source code of this exact version: git tag v1.0.0 at
  https://github.com/etcofficials/LumaCapture
