<#
.SYNOPSIS
  Runs the LumaCapture tests. Short and light by design (no stress or long runs).
    powershell -File scripts\test.ps1              unit tests only
    powershell -File scripts\test.ps1 -Hardware    + webcam/audio/short-recording/crash-recovery tests
    powershell -File scripts\test.ps1 -Gui         + scripted GUI self-test (records ~6 s of the screen)
  Output: G:\LumaCapture\build\test-output (never the user's recording folder).
  The CPU clock is sampled during hardware/GUI tests; a run stops if the CPU
  falls below 60 % of its base clock (thermal throttling).
#>
param([switch]$Hardware, [switch]$Gui)
$ErrorActionPreference = 'Stop'
$bin = 'G:\LumaCapture\build\release\bin'
$out = 'G:\LumaCapture\build\test-output'
New-Item -ItemType Directory -Force $out | Out-Null
$env:TEMP = 'G:\LumaCapture\build\tmp'; $env:TMP = $env:TEMP

function Invoke-Watched([string]$exe, [string[]]$arguments, [string]$log, [int]$timeoutSec) {
    $sp = @{ FilePath = $exe; NoNewWindow = $true; PassThru = $true; RedirectStandardOutput = $log }
    if ($arguments.Count -gt 0) { $sp.ArgumentList = $arguments }
    $p = Start-Process @sp
    $null = $p.Handle    # keep the handle open, otherwise ExitCode is empty after exit
    $t0 = Get-Date
    while (-not $p.HasExited) {
        $clock = (Get-Counter '\Processor Information(_Total)\% Processor Performance' -SampleInterval 2 -MaxSamples 1).CounterSamples[0].CookedValue
        if ($clock -lt 60) { $p.Kill(); throw "Stopped: CPU throttling detected (clock $([int]$clock)% of base)" }
        if (((Get-Date) - $t0).TotalSeconds -gt $timeoutSec) { $p.Kill(); throw "Stopped: timeout after $timeoutSec s" }
    }
    $p.WaitForExit()
    return $p.ExitCode
}

$args1 = @()
if ($Hardware) { $args1 += '--hw' }
$code = Invoke-Watched "$bin\luma-tests.exe" $args1 "$out\luma-tests.txt" 300
Get-Content "$out\luma-tests.txt" | Select-String '^\[PASS\]|^\[FAIL\]|^\s{12}|passed' | ForEach-Object { "$_" }
if ($code -ne 0) { throw "luma-tests failed (exit $code)" }

if ($Gui) {
    $guiOut = "$out\gui"
    New-Item -ItemType Directory -Force $guiOut | Out-Null
    $code = Invoke-Watched "$bin\LumaCapture.exe" @('--selftest', '6', '--output', $guiOut) "$out\gui-selftest.txt" 200
    $log = Get-ChildItem "$bin\LumaCapture-data\logs\*.log" | Sort-Object LastWriteTime | Select-Object -Last 1
    Get-Content $log.FullName | Select-String 'SELFTEST|state:|ignored|Recording (started|stopped)' | Select-Object -Last 25 | ForEach-Object { "$_" }
    if ($code -ne 0) { throw "GUI self-test failed (exit $code)" }
    Write-Host "GUI self-test passed"
}
