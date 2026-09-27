<#
.SYNOPSIS
  Configure + build LumaCapture with the portable MSVC toolchain on G:.
  Usage:  powershell -File G:\LumaCapture\scripts\build.ps1 [-Preset release|debug]
#>
param([ValidateSet('release', 'debug')][string]$Preset = 'release', [switch]$KeepGoing)
$ErrorActionPreference = 'Stop'

. G:\DevTools\setup-msvc.ps1 -Quiet
$env:PATH = "G:\DevTools\CMake\bin;G:\DevTools\Ninja;$env:PATH"
# Keep compiler temp files off C:.
$tmp = 'G:\LumaCapture\build\tmp'
New-Item -ItemType Directory -Force $tmp | Out-Null
$env:TEMP = $tmp; $env:TMP = $tmp

Push-Location G:\LumaCapture
try {
    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }
    if ($KeepGoing) { cmake --build --preset $Preset -- -k 0 } else { cmake --build --preset $Preset }
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
} finally { Pop-Location }
