LumaCapture 2.0.0 - portable screen recorder for Windows (x64)
================================================================

LumaCapture is a lightweight screen recorder made for older PCs: screen,
window, region or game, with system audio, microphone and an optional webcam
overlay. No accounts, no telemetry, no internet access.

Project page and source code: https://github.com/etcofficials/LumaCapture

INSTALL (portable - nothing is installed)
  1. Extract this whole ZIP (right-click > "Extract All..."). Do not run it
     from inside the ZIP.
  2. Put the "LumaCapture" folder anywhere, for example D:\Apps\LumaCapture.
     Paths with spaces are fine; no administrator rights are needed.
  3. Open the folder and double-click LumaCapture.exe.
     (Launch-LumaCapture.bat does the same; Create-Desktop-Shortcut.bat adds a
     desktop shortcut after asking.)
  Prefer a normal installation with Start Menu entry and uninstaller? Use
  LumaCapture-v2.0.0-Setup.exe from the same release page instead.

  If Windows SmartScreen says "Windows protected your PC", click
  "More info" > "Run anyway". The app is not code-signed.

  To remove the portable version, delete the LumaCapture folder. Settings and
  the library list are in LumaCapture\LumaCapture-data; recordings stay where
  you saved them.

RECORD IN FIVE SECONDS
  1. Choose Display, Window, Region or Game (left).
  2. Optional: switch on Webcam, Microphone or System Audio (Sources).
  3. Press the red Record button (or Ctrl+Alt+R). Press Stop to finish.
  4. The file appears under Recent Recordings and in the Library
     (Play, Show in folder, Rename, Delete...).
  Recordings go to your Videos\LumaCapture folder by default (Output).
  Hotkeys: Ctrl+Alt+R start/stop, Ctrl+Alt+P pause, Ctrl+Alt+M mute mic,
  Ctrl+Alt+X screenshot. Full guide: LumaCapture\README.txt.

SYSTEM REQUIREMENTS
  - Windows 10 (1809 or later; 2004 or later recommended) or Windows 11, 64-bit.
  - A DirectX 11 graphics card. No hardware video encoder is needed: video is
    encoded on the CPU with x264.

HARDWARE LIMITATIONS - PLEASE READ
  - The CPU decides what is possible. On an old quad-core (developed on an
    Intel i5-2400S) 1080p at 30 FPS and 720p at 60 FPS are the recommended
    settings; 1080p at 60 FPS is marked "experimental" and is likely to drop
    frames. The Video tab shows an estimate for every setting.
  - Watch the Dropped frames value (Settings > Advanced > performance panel).
    LumaCapture warns when a recording falls behind.
  - PCs that overheat slow down during long recordings.
  - Webcams in dim rooms may stutter because the sensor exposes longer than one
    frame; add light, or use Webcam > Camera & lighting > Low light: Smooth
    motion together with Auto adjust.
  - Window capture on Windows 10 shows a yellow border drawn by Windows.
  - No echo cancellation: use headphones with a microphone.

IF SOMETHING GOES WRONG
  - After a crash or power loss LumaCapture offers to recover the recording
    the next time it starts (at most about one second is lost).
  - Logs and crash reports stay on your PC (Settings > Advanced). Please attach
    the newest log when reporting a problem:
    https://github.com/etcofficials/LumaCapture/issues
  - Feedback: Instagram @etcofficials, e-mail etcofficials28@gmail.com

LICENSE
  LumaCapture is free software under the GNU General Public License v3.0
  (LumaCapture\licenses\GPL-3.0.txt). It includes FFmpeg (GPL-3.0 build with
  x264) and Qt 6 (LGPL-3.0); see LumaCapture\licenses\THIRD-PARTY-NOTICES.txt.
  Source code of this exact version: git tag v2.0.0 at
  https://github.com/etcofficials/LumaCapture
