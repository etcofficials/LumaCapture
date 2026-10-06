# Testing - LumaCapture 2.0.0

## Status: NOT BUILT, NOT TESTED

Version 2.0.0 was written while the development PC's G: drive (Seagate ST500DM002) was
logging escalating hardware read errors (Windows events 153, 7 and 154 since August 2026;
the compiler crashed with "in-page error" on 2026-10-05). On the owner's instruction the
v2 code was **edited and saved only - no builds, tests, benchmarks or app launches**.
Nothing below the "Test plan" heading has been run against v2.

## Measurements made during the v2 work (before the edit-only phase)

### Encoder study (offline, on the target PC)

Lossless 10 s 1080p30 references: a scrolling text/code "screen" page with a video inset,
and a natural-motion clip. Each candidate encoded with the app's settings (libx264, 3
threads, 2 s keyframes, BT.709 limited); quality measured against the reference with
timestamps aligned (`settb=1/30,setpts=N`). CPU = user CPU seconds per frame.

| Content | Setting | Mbit/s | CPU s/frame | VMAF | SSIM Y | PSNR Y |
|---|---|---|---|---|---|---|
| screen | ultrafast CRF 23 (v1 default) | 4.88 | 0.0327 | 99.96 | 0.9979 | 50.7 |
| screen | ultrafast CRF 18 | 7.50 | 0.0328 | 99.96 | 0.9990 | 54.8 |
| screen | ultrafast CRF 21 + deblock | 5.53 | 0.0340 | 99.96 | 0.9984 | 52.3 |
| screen | superfast CRF 23 | 3.29 | 0.0415 | 99.95 | 0.9977 | 47.2 |
| screen | veryfast CRF 23 | 1.80 | 0.0503 | 99.93 | 0.9955 | 42.9 |
| motion | ultrafast CRF 23 (v1 default) | 7.67 | 0.0351 | 95.97 | 0.9897 | 45.9 |
| motion | ultrafast CRF 18 | 13.55 | 0.0383 | 96.98 | 0.9943 | 49.3 |
| motion | ultrafast CRF 21 + deblock | 9.43 | 0.0389 | 96.43 | 0.9925 | 47.4 |
| motion | superfast CRF 23 | 3.80 | 0.0496 | 93.57 | 0.9894 | 43.7 |
| motion | veryfast CRF 23 | 2.26 | 0.0633 | 91.79 | 0.9870 | 42.3 |

Conclusion used for the v2 presets: on this 4-core Sandy Bridge, x264 "ultrafast" with
deblocking and a lower CRF gives the best quality per CPU second; slower presets cost
40-80 % more CPU and score lower at the same CRF. The CPU clock fell to ~80 % of base
within seconds of the slower runs (thermal throttling; earlier benchmarks saw 45 %).

### Hang analysis of 1.0 (from the user's hang dump, 2026-10-05 22:49)

UI thread stack: `MainWindow::closeEvent -> saveSettings -> QSettings::sync ->
QLockFile::tryLock -> FlushFileBuffers` - blocked on the failing G: drive (disk error
events at 22:47-22:53). Fixed in v2 by moving all settings/history writes to a
background disk worker.

## Results of 1.0.1 (for reference)

17 automated tests (unit + hardware) and the scripted GUI self-test passed on
2026-09-27 on the target PC; see git history of this file for the details.

## Test plan for 2.0.0 (to run once the build environment is reliable)

Short, controlled runs only (5-10 s first, then at most 30-60 s); stop if the CPU clock
falls below 60 % of base. `scripts\test.ps1` samples the clock automatically.

1. **Build:** `scripts\build.ps1`; fix compile errors (v2 was never compiled).
2. **Unit + hardware tests:** `scripts\test.ps1 -Hardware` - includes the new
   `capture_exclusion_in_recorded_file` probe (records the screen with test windows of
   each kind and decodes the file to see which ones appear).
3. **GUI self-test:** `scripts\test.ps1 -Gui` (record, pause, resume, double stop, webcam
   on/off, webcam tab, settings page).
4. **Own UI not in the recording:** record a display with the main window and the HUD
   visible; decode frames; confirm neither appears.
5. **Matrix (10 s each, cool-down between):** 720p30, 1080p30, 720p60, 1080p60 with the
   motion clip playing - verify for each file: resolution, frame rate (avg and per-frame
   timestamps), codec, pixel format (yuv420p), audio codec / sample rate, duration, size,
   captured/encoded/dropped/repeated frames, latencies, CPU/RAM/GPU; decode sample frames.
6. **Features:** window, region and game capture; webcam + overlay + Auto adjust;
   system audio + mic; screenshot (and that it excludes LumaCapture); library (thumbnails,
   rename, delete, details); import + drag & drop (valid, invalid and large files);
   hotkeys while another program has focus; crash recovery.
7. **Packaging:** `scripts\package.ps1` (dependency check, launch from a path with spaces);
   `scripts\make-release.ps1 -Version 2.0.0`; install the Setup.exe into a G: folder,
   check Start Menu / Desktop shortcuts, icon, uninstall entry, then uninstall.
