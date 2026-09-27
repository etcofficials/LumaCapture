"""Builds benchmark-report.txt from the luma-bench CSV and the player stats.

Merges the workload columns (player late pictures, player CPU, measured
desktop update rate) into benchmark-results.csv so one CSV holds everything.

Usage: python scripts/bench-report.py [--dir G:/video/LumaCapture-Bench] [--observations FILE]
"""
import argparse
import csv
import datetime
import pathlib
import re

MATRIX_ORDER = [f"{r}{f}-{p}" for r in ("1080p", "720p") for f in (30, 60)
                for p in ("ultrafast", "superfast", "veryfast")]
CONTROL = ["ctrl-1080p30-superfast-cpuconv", "ctrl-1080p30-superfast-gpuconv"]


def f(v, nd=1):
    try:
        return f"{float(v):.{nd}f}"
    except (TypeError, ValueError):
        return str(v)


def load(directory):
    rows = list(csv.DictReader(open(directory / "benchmark-results.csv", newline="", encoding="utf-8")))
    player = {}
    pfile = directory / "player-stats.csv"
    if pfile.exists():
        for p in csv.DictReader(open(pfile, newline="", encoding="utf-8")):
            player[p["label"]] = p
    for r in rows:
        p = player.get(r["label"], {})
        r["player_late_or_dropped_pictures"] = p.get("player_late_or_dropped_pictures", "n/a")
        r["player_cpu_percent"] = p.get("player_cpu_percent", "n/a")
        r["cooldown_s"] = p.get("cooldown_s", "n/a")
        r.update(system_stats(directory / "matrix-logs" / f"{r['label']}.system.csv"))
        dur = float(r["duration_s"]) or 1.0
        r["workload_desktop_updates_per_s"] = f"{int(r['desktop_copies']) / dur:.1f}"
    return rows


def system_stats(path):
    """Averages of the 5 s attribution samples taken during a run."""
    out = {"cpu_clock_avg_pct_of_base": "n/a", "cpu_clock_min_pct_of_base": "n/a", "other_top_processes": "n/a"}
    if not path.exists():
        return out
    rows = list(csv.DictReader(open(path, newline="", encoding="ascii")))
    rows = rows[1:] if len(rows) > 2 else rows  # first sample overlaps start-up
    clocks = [float(r["cpu_clock_pct_of_base"]) for r in rows if r["cpu_clock_pct_of_base"]]
    if clocks:
        out["cpu_clock_avg_pct_of_base"] = f"{sum(clocks) / len(clocks):.1f}"
        out["cpu_clock_min_pct_of_base"] = f"{min(clocks):.1f}"
    others = {}
    for r in rows:
        # "name=value" pairs; names may contain spaces ("memory compression")
        for name, val in re.findall(r"\s*(.+?)=([\d.]+)", r["top_processes_pct_of_machine"] or ""):
            if name not in ("luma-bench", "vlc"):
                others[name] = others.get(name, 0.0) + float(val) / len(rows)
    top = sorted(others.items(), key=lambda kv: -kv[1])[:3]
    out["other_top_processes"] = " ".join(f"{n}={v:.1f}%" for n, v in top)
    return out


def write_csv(directory, rows):
    fields = list(rows[0].keys())
    with open(directory / "benchmark-results.csv", "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


def table(headers, data):
    widths = [max(len(str(h)), *(len(str(row[i])) for row in data)) for i, h in enumerate(headers)]
    line = lambda cells: "  ".join(str(c).ljust(widths[i]) for i, c in enumerate(cells)).rstrip()
    out = [line(headers), line(["-" * w for w in widths])]
    out += [line(row) for row in data]
    return "\n".join(out)


def report(directory, rows, observations):
    by = {r["label"]: r for r in rows}
    matrix = [by[l] for l in MATRIX_ORDER if l in by]
    control = [by[l] for l in CONTROL if l in by]
    missing = [l for l in MATRIX_ORDER + CONTROL if l not in by]
    L = []
    L.append("LumaCapture luma-bench - performance benchmark report")
    L.append("=" * 72)
    L.append(f"Generated: {datetime.datetime.now():%Y-%m-%d %H:%M}")
    L.append("""
System under test
  CPU     Intel Core i5-2400S (Sandy Bridge, 4C/4T, 2.5 GHz, AVX, no AVX2)
  RAM     12 GB DDR3
  GPU     NVIDIA GeForce GT 730 (GF108 Fermi, 4 GB DDR3), driver 391.35, WDDM 2.3, D3D FL 11_0
  OS      Windows 10 Pro 22H2 (19045)
  Display 1920x1080 @ 60 Hz, single monitor
  Output  G: (7200 rpm HDD)
  Encoder libx264 via FFmpeg n7.1.5 (software; this GPU has no hardware encoder)

Fixed settings (all runs)
  CRF 23, 3 x264 threads, keyframe every 2 s, GPU NV12 conversion (except the
  --cpu-convert control run), MKV output, bounded queue of 8 frames,
  3-slot readback ring, cursor capture on, 60 s per run.

Workload
  Local clip "Japanese Autumn Leaves 1080p60" (H.264 1920x1080 60 fps, 43.7 s,
  looped) played full-screen by VLC with software decoding, restarted before
  every run so each run sees identical content. The screen therefore changes
  ~60 times per second (see "Desktop upd/s", measured by Desktop Duplication).
  Why VLC software decoding: measured before the matrix, ffplay (~44 fps) and
  VLC with DXVA2/D3D11VA hardware decoding dropped frames on the GT 730 even
  with no capture running; VLC software decoding presented a steady 60 fps.
  The player's own CPU use and late pictures are listed per run; the player
  load is part of the "system CPU" figure, as it would be when recording a
  real video/game.

Classification rules
  NOT REAL-TIME        encoder capacity < 1.0x real time, OR > 1 % of capture ticks
                       dropped, OR the worst 1 s window encoded < 90 % of target fps,
                       OR encode latency / queue depth trend upward over the run
                       (last 35 % vs 5-35 % of the run: latency +25 % and > 2 frame
                       periods, or queue depth +2 frames).
  REAL-TIME WITH DROPS otherwise, if any frame was dropped or the queue reached capacity.
  REAL-TIME STABLE     otherwise.

Metric definitions
  FPS avg/min          frames entering the encoder queue per 1 s window (first s excluded)
  Enc FPS min          packets leaving the encoder per 1 s window (first s excluded)
  Capture latency      capture tick -> NV12 frame in the encoder queue
  Encode latency       queue entry -> compressed packet out of x264 (includes x264
                       lookahead/B-frame/frame-thread delay, i.e. mostly pipeline depth)
  Enc cap              encoder throughput = frames / time spent inside libx264 calls,
                       divided by target fps (>1.0 = keeps up)
  CPU proc / sys       luma-bench process / whole machine, % of all 4 cores
  GPU busy / own       busiest GPU engine, all processes / this process (PDH counters)
  RAM                  luma-bench working set (max over the run)
""")
    headers = ["Config", "Class", "FPS avg", "FPS min", "Enc min", "Captured", "Encoded", "Dropped", "Drop reason",
               "CapLat avg", "CapLat p95", "EncLat avg", "EncLat 1st->last", "Q max", "Q filled", "GPU stalls",
               "Enc cap", "CPU proc", "CPU sys avg/max", "GPU busy/own", "RAM MB", "Mbps", "File MB",
               "Desktop upd/s", "Player late", "Player CPU", "Clock avg/min", "Cool-down s"]

    def row(r):
        return [r["label"], r["classification"], f(r["capture_fps_avg"], 2), f(r["capture_fps_min"], 2),
                f(r["encode_fps_min"], 2), r["frames_captured"], r["frames_encoded"], r["frames_dropped"],
                r["drop_reason"], f(r["capture_latency_avg_ms"]), f(r["capture_latency_p95_ms"]),
                f(r["encode_latency_avg_ms"]), f"{f(r['encode_latency_first_ms'], 0)}->{f(r['encode_latency_last_ms'], 0)}",
                r["queue_depth_max"], r["queue_filled"], r["gpu_stalls"], f(r["encoder_realtime_factor"], 2) + "x",
                f(r["cpu_process_avg"]) + "%", f"{f(r['cpu_system_avg'])}/{f(r['cpu_system_max'])}%",
                f"{r['gpu_busiest_avg']}/{r['gpu_process_avg']}%", f(r["ram_ws_max_mb"]), f(r["bitrate_mbps"], 2),
                f(int(r["file_size_bytes"]) / 1048576, 1), r["workload_desktop_updates_per_s"],
                r["player_late_or_dropped_pictures"], r["player_cpu_percent"] + "%",
                f"{r['cpu_clock_avg_pct_of_base']}/{r['cpu_clock_min_pct_of_base']}%", r["cooldown_s"]]

    L.append("MAIN MATRIX (GPU conversion)")
    L.append("-" * 72)
    L.append(table(headers, [row(r) for r in matrix]))
    L.append("")
    L.append("Classification grid")
    grid = []
    for res in ("1080p", "720p"):
        for fps in (30, 60):
            cells = [f"{res}{fps}"]
            for p in ("ultrafast", "superfast", "veryfast"):
                r = by.get(f"{res}{fps}-{p}")
                cells.append(r["classification"] if r else "NOT RUN")
            grid.append(cells)
    L.append(table(["", "ultrafast", "superfast", "veryfast"], grid))
    L.append("")
    L.append("Per-run classification reasons")
    for r in matrix:
        L.append(f"  {r['label']:<22} {r['classification']:<22} {r['classification_reason'] or '-'}")
    L.append("")
    L.append("Other processes during each run (average % of whole CPU)")
    for r in matrix + control:
        L.append(f"  {r['label']:<32} {r['other_top_processes']}")
    L.append("")
    L.append("CONTROLLED COMPARISON: 1080p30 superfast CRF 23, CPU vs GPU conversion")
    L.append("-" * 72)
    if control:
        L.append(table(headers, [row(r) for r in control]))
        L.append("")
        ch = ["Config", "Convert ms/frame", "Readback ms/frame", "CPU proc avg", "CPU sys avg", "GPU own avg",
              "GPU busy avg", "CapLat avg"]
        L.append(table(ch, [[r["label"], f(r["cpu_convert_ms_per_frame"], 2), f(r["readback_ms_per_frame"], 2),
                             f(r["cpu_process_avg"]) + "%", f(r["cpu_system_avg"]) + "%",
                             r["gpu_process_avg"] + "%", r["gpu_busiest_avg"] + "%",
                             f(r["capture_latency_avg_ms"])] for r in control]))
    L.append("")
    L.append("Runs not completed")
    L.append("-" * 72)
    failed = [r["label"] for r in rows if r["status"] != "ok"]
    if not missing and not failed:
        L.append("  None: all 12 matrix runs and both control runs completed.")
    for l in missing:
        L.append(f"  {l}: no result row (did not run or crashed before writing results)")
    for l in failed:
        L.append(f"  {l}: pipeline reported a failure (see matrix-logs/{l}.bench.txt)")
    L.append("")
    if observations:
        L.append("OBSERVATIONS")
        L.append("-" * 72)
        L.append(observations.read_text(encoding="utf-8").rstrip())
        L.append("")
    L.append("Files")
    L.append("  benchmark-results.csv   one row per run, all measured values")
    L.append("  matrix-logs/*.bench.txt full luma-bench output per run (live samples + summary)")
    L.append("  matrix-logs/*.vlc.log   player log per run")
    L.append("  bench_*.mkv / bench_*.log recordings and per-run logs")
    (directory / "benchmark-report.txt").write_text("\n".join(L) + "\n", encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="G:/video/LumaCapture-Bench")
    ap.add_argument("--observations")
    a = ap.parse_args()
    d = pathlib.Path(a.dir)
    rows = load(d)
    write_csv(d, rows)
    report(d, rows, pathlib.Path(a.observations) if a.observations else None)
    print(f"wrote {d / 'benchmark-report.txt'} ({len(rows)} runs)")


if __name__ == "__main__":
    main()
