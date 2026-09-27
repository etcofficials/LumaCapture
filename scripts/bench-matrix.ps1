<#
.SYNOPSIS
  Runs the luma-bench matrix against a full-screen looping motion video.

  Workload: VLC plays a local 1080p60 clip full-screen with SOFTWARE decoding.
  (Measured on this PC: ffplay and VLC's DXVA2/D3D11VA paths could not present
  60 fps on the GT 730 even without capture; VLC software decode presents a
  steady 60 fps at ~10 % CPU.)

  For every run: start VLC fresh (every run sees identical content), wait for
  steady playback, run luma-bench, stop VLC, record the player's late/dropped
  pictures and CPU during the benchmark window. Settings are fixed per run.

  Usage: powershell -File scripts\bench-matrix.ps1 [-Only <label,...>] [-Duration 60]
#>
param(
    [string]$Video = 'G:\DownloAD\vidssave.com Live Wallpaper Japanese Autumn Leaves 1080p60.mp4',
    [int]$Duration = 60,
    [string[]]$Only = @(),
    [string]$OutDir = 'G:\video\LumaCapture-Bench',
    [switch]$Append
)
$ErrorActionPreference = 'Stop'

$bench  = 'G:\LumaCapture\build\release\bin\luma-bench.exe'
$vlc    = 'C:\Program Files\VideoLAN\VLC\vlc.exe'
$csv    = Join-Path $OutDir 'benchmark-results.csv'
$player = Join-Path $OutDir 'player-stats.csv'
$logDir = Join-Path $OutDir 'matrix-logs'
New-Item -ItemType Directory -Force $logDir | Out-Null
if (-not (Test-Path $Video)) { throw "Motion video not found: $Video" }

if (-not $Append) {
    Remove-Item -ErrorAction SilentlyContinue -LiteralPath $csv
    Remove-Item -ErrorAction SilentlyContinue -LiteralPath $player
}
if (-not (Test-Path $player)) {
    'label,player_late_or_dropped_pictures,player_cpu_percent,cooldown_s,player_log' | Out-File -Encoding ascii $player
}

# VLC logs "picture is too late to be displayed" / "dropping" for every picture it fails to show on time.
function Get-PlayerLate([string]$log) {
    if (-not (Test-Path $log)) { return 0 }
    $fs = [System.IO.File]::Open($log, 'Open', 'Read', 'ReadWrite') # VLC still writes the file
    try { $text = (New-Object System.IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
    return [regex]::Matches($text, 'too late|dropping').Count
}

# Fixed matrix: CRF 23, 3 threads, GPU conversion unless stated.
$runs = @()
foreach ($res in @(@{n='1080p'; r='1920x1080'}, @{n='720p'; r='1280x720'})) {
    foreach ($fps in 30, 60) {
        foreach ($preset in 'ultrafast', 'superfast', 'veryfast') {
            $runs += [pscustomobject]@{ Label = "$($res.n)$fps-$preset"; Res = $res.r; Fps = $fps; Preset = $preset; Cpu = $false }
        }
    }
}
$runs += [pscustomobject]@{ Label = 'ctrl-1080p30-superfast-cpuconv'; Res = '1920x1080'; Fps = 30; Preset = 'superfast'; Cpu = $true }
$runs += [pscustomobject]@{ Label = 'ctrl-1080p30-superfast-gpuconv'; Res = '1920x1080'; Fps = 30; Preset = 'superfast'; Cpu = $false }
if ($Only.Count) { $runs = @($runs | Where-Object { $Only -contains $_.Label }) }

$i = 0
foreach ($r in $runs) {
    $i++
    Write-Host "=== [$i/$($runs.Count)] $($r.Label) ==="
    Get-Process vlc -ErrorAction SilentlyContinue | Stop-Process -Force

    # This PC throttles to ~45 % of base clock under sustained load and recovers
    # slowly. Start every run from the same state: wait until the clock is back
    # at >= 95 % of base and the machine is idle (2 consecutive samples, max 180 s).
    $cool0 = Get-Date; $ok = 0
    while ($ok -lt 2 -and ((Get-Date) - $cool0).TotalSeconds -lt 180) {
        $cs = (Get-Counter -SampleInterval 3 -MaxSamples 1 -Counter @(
            '\Processor Information(_Total)\% Processor Performance', '\Processor(_Total)\% Processor Time')).CounterSamples
        if ($cs[0].CookedValue -ge 95 -and $cs[1].CookedValue -lt 30) { $ok++ } else { $ok = 0 }
    }
    $coolSec = [int]((Get-Date) - $cool0).TotalSeconds
    Write-Host ("    cool-down {0}s, start clock {1:N0}% of base" -f $coolSec, $cs[0].CookedValue)

    $plog = Join-Path $logDir "$($r.Label).vlc.log"
    $p = Start-Process -FilePath $vlc -PassThru -ArgumentList @(
        '--fullscreen', '--loop', '--no-audio', '--video-on-top', '--no-video-title-show', '--no-qt-fs-controller',
        '--no-qt-privacy-ask', '--no-qt-error-dialogs', '--no-osd', '--avcodec-hw=none',
        '--file-logging', "--logfile=`"$plog`"", '--log-verbose=2', "`"$Video`"")
    Start-Sleep -Seconds 5   # window up, full-screen, decoding at steady state
    $lateBefore = Get-PlayerLate $plog
    $cpuBefore = (Get-Process -Id $p.Id).TotalProcessorTime.TotalSeconds
    $t0 = Get-Date

    $benchArgs = @('--resolution', $r.Res, '--fps', $r.Fps, '--preset', $r.Preset, '--crf', 23, '--threads', 3,
                   '--duration', $Duration, '--report-interval', 10, '--csv', $csv, '--label', $r.Label, '--out-dir', $OutDir)
    if ($r.Cpu) { $benchArgs += '--cpu-convert' }
    # Attribution sampler: CPU clock vs base and the top CPU consumers (PDH sees
    # protected processes such as MsMpEng/dwm, unlike Get-Process), every 5 s.
    $attrFile = Join-Path $logDir "$($r.Label).system.csv"
    $job = Start-Job -ArgumentList $attrFile, $Duration -ScriptBlock {
        param($file, $dur)
        'elapsed_s,cpu_total_pct,cpu_clock_pct_of_base,top_processes_pct_of_machine' | Out-File -Encoding ascii $file
        $cpus = [Environment]::ProcessorCount
        $t0 = Get-Date
        while (((Get-Date) - $t0).TotalSeconds -lt $dur) {
            $s = (Get-Counter -SampleInterval 5 -MaxSamples 1 -ErrorAction SilentlyContinue -Counter @(
                '\Processor(_Total)\% Processor Time', '\Processor Information(_Total)\% Processor Performance',
                '\Process(*)\% Processor Time')).CounterSamples
            $total = ($s | Where-Object { $_.Path -like '*\processor(_total)\*' }).CookedValue
            $perf = ($s | Where-Object { $_.Path -like '*processor performance' }).CookedValue
            $top = $s | Where-Object { $_.Path -like '*\process(*' -and $_.InstanceName -notin '_total', 'idle' } |
                Sort-Object CookedValue -Descending | Select-Object -First 6 |
                ForEach-Object { '{0}={1:N1}' -f $_.InstanceName, ($_.CookedValue / $cpus) }
            '{0:N0},{1:N1},{2:N1},{3}' -f ((Get-Date) - $t0).TotalSeconds, $total, $perf, ($top -join ' ') |
                Out-File -Encoding ascii -Append $file
        }
    }
    $ErrorActionPreference = 'Continue'   # x264 writes its banner to stderr
    & $bench @benchArgs 2>&1 | ForEach-Object { "$_" } | Out-File -Encoding utf8 (Join-Path $logDir "$($r.Label).bench.txt")
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    Wait-Job $job -Timeout 15 | Out-Null
    Remove-Job $job -Force

    $alive = -not $p.HasExited
    $late = 'player-exited'; $cpu = 'n/a'
    if ($alive) {
        $cpu = '{0:N1}' -f (((Get-Process -Id $p.Id).TotalProcessorTime.TotalSeconds - $cpuBefore) /
                            ((Get-Date) - $t0).TotalSeconds / [Environment]::ProcessorCount * 100)
        $late = (Get-PlayerLate $plog) - $lateBefore
        Stop-Process -Id $p.Id -Force
    }
    "$($r.Label),$late,$cpu,$coolSec,$plog" | Out-File -Encoding ascii -Append $player
    Write-Host "    exit=$code player late/dropped=$late player cpu=$cpu%"
}
Write-Host "MATRIX DONE"
