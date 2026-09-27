# Testing - LumaCapture 1.0.0

These are the results on the development PC: i5-2400S (4C/4T, AVX, no AVX2),
12 GB RAM, GeForce GT 730 (Fermi), Windows 10 22H2, 1920×1080 @ 60 Hz, and a
Logitech C270 webcam. All runs were short by design: no stress tests, long
recordings or thermal loads. The CPU clock was sampled during the hardware and
GUI runs; it stayed at about 110 % of base, so no throttling occurred.

## How to run

```
powershell -File scripts\test.ps1              # 11 unit tests (no devices needed)
powershell -File scripts\test.ps1 -Hardware    # + 6 hardware tests (webcam, audio, screen, crash recovery)
powershell -File scripts\test.ps1 -Gui         # + scripted GUI self-test (records ~6 s of the screen)
```

Output goes to `build\test-output` and never to the recording folder. The
script stops a run if the CPU falls below 60 % of its base clock.

## Automated results (release build)

| Test | Result | Notes |
|---|---|---|
| state_machine_happy_path / rejects_invalid_requests | PASS | double start/stop, pause while idle etc. rejected |
| pause_timeline_offsets | PASS | |
| audio_ring_positions_and_silence | PASS | |
| bounded_queue_never_blocks_producer / close_wakes_consumer | PASS | |
| frame_exchange_latest_frame_and_no_blocking | PASS | |
| frame_pool_is_bounded_and_reusable | PASS | |
| webcam_processor_identity_and_fastpath | PASS | |
| webcam_chroma_key_removes_green | PASS | |
| auto_adjust_is_deterministic_and_sensible | PASS | |
| hw webcam_controls_and_pacing | PASS | 10 controls read; 31 fps; filter chain 1.1 ms/frame |
| hw webcam_exposure_priority_effect | PASS | original setting restored afterwards |
| hw recording_lifecycle_short | PASS | 720p30 + system + mic + webcam, pause; start 138 ms, stop 200 ms, 0 dropped; video 5.03 s = audio 5.03 s |
| hw mp4_conversion | PASS | 5.03 s |
| hw crash_recovery | PASS | process killed while recording; recovered file decodes (62 frames) |
| hw webcam_start_stop_cycles | PASS | 6 cycles, each stop 474-523 ms (the earlier internal build could hang here indefinitely) |
| GUI self-test | PASS | see below |

GUI self-test sequence:
1. Start twice (the second press is ignored).
2. Pause, then resume.
3. Stop twice (the second press is ignored).
4. Wait for "saved", then check the file.
5. Turn the webcam on, then off.
6. Open and close the webcam settings page.

The state path observed was Idle → Starting → Recording → Paused → Recording →
Stopping → Finalizing → Idle. The run encoded 188 frames with 0 dropped. The
file was 1920×1080 H.264 + AAC, 6.3 s long, and `ffmpeg -f null` decoded it
without errors.

Packaging check (`scripts\package.ps1`):
- Every DLL import of every packaged binary resolves inside the package or to
  Windows.
- A copy of the package in a path with spaces started through
  `Launch-LumaCapture.bat` from `C:\Windows` as the working directory, with
  PATH limited to Windows folders, and rendered its UI.

## Webcam investigation ("shutter" / stutter)

Measured on the C270 in the test room:
- The camera ran with auto-exposure at about 62 ms (-4), gain 113 and
  exposure priority ON.
- It delivered 31 fps on average, but the longest gap between frames was about
  64 ms.
- With exposure priority OFF, the gap stayed about 64 ms and the picture became
  much darker (median luma 0.27 → 0.05).
- Conclusion: the room light is too low for the sensor at 30 fps. The camera
  exposes longer than one frame. This is a physical limit, not mains-light
  flicker and not a LumaCapture slowdown.
- Mitigations in the app:
  - an Anti-flicker control;
  - a Low-light priority choice;
  - Auto adjust, which brightens in software instead of lengthening the
    exposure;
  - a live pacing readout that explains the effect.

Older measurements: the earlier internal build's synchronous reader delivered
about 24 fps from the same camera; this release's asynchronous reader delivers
about 30 fps. The preview was 15 fps before and now runs at the camera rate.

## Not tested (or only partly)

Automated tests do not cover these, and they were not tested manually during
this release. Use the checklist in `docs/USER-GUIDE.txt`.

- Window capture and region capture through the GUI. The capture code is
  unchanged since the earlier internal build.
- Global hotkeys pressed while another application has focus.
- Microphone processing chains (gate, compressor, EQ, noise suppression) by ear.
- Audio device unplug/replug and default-device changes while recording.
- Webcam unplug while recording. There is code for it (a retry every 3 s and a
  5 s stall detection), but it was not exercised.
- GPU device loss (driver reset).
- The low-disk auto-stop at 300 MB free.
- Light theme (only the dark theme was reviewed in snapshots).
- Layout dialog interaction, overlays, screenshots, and the countdown and
  floating bar.
- Recordings longer than about 10 s, and 1080p60 in the GUI. Earlier
  `luma-bench` runs showed 1080p60 is too heavy for this CPU.
- Other webcams (only the C270 was available).
