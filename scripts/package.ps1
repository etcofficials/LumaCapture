<#
.SYNOPSIS
  Builds LumaCapture (Release) and assembles the portable distribution:
      G:\LumaCapture\dist\LumaCapture\
  Contents: LumaCapture.exe, Launch-LumaCapture.bat, Create-Desktop-Shortcut.bat,
  luma-bench.exe, Qt runtime (windeployqt), FFmpeg DLLs, MSVC runtime (app-local),
  licenses\, README.txt.
  An existing LumaCapture-data folder (the user's settings, history, logs) is kept.
  Then verifies the package: every DLL import resolves inside the folder or to
  Windows, and a copy in a path with spaces starts via the launcher with a minimal PATH.
  Everything stays on G:; temp files go to G:\LumaCapture\build\tmp.

  Usage: powershell -File G:\LumaCapture\scripts\package.ps1 [-SkipBuild] [-SkipVerify]
#>
param([switch]$SkipBuild, [switch]$SkipVerify)
$ErrorActionPreference = 'Stop'

$root  = 'G:\LumaCapture'
$bin   = "$root\build\release\bin"
$dist  = "$root\dist\LumaCapture"
$tmp   = "$root\build\tmp"
New-Item -ItemType Directory -Force $tmp | Out-Null
$env:TEMP = $tmp; $env:TMP = $tmp

if (-not $SkipBuild) {
    & "$root\scripts\build.ps1" -Preset release
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}
if (-not (Test-Path "$bin\LumaCapture.exe")) { throw "Build output not found: $bin\LumaCapture.exe" }

# Clean the previous package but NEVER the user's data folder.
New-Item -ItemType Directory -Force $dist | Out-Null
Get-ChildItem -LiteralPath $dist -Force | Where-Object { $_.Name -ne 'LumaCapture-data' } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }
New-Item -ItemType Directory -Force "$dist\licenses" | Out-Null

Copy-Item "$bin\LumaCapture.exe", "$bin\luma-bench.exe" $dist
Copy-Item "$root\packaging\Launch-LumaCapture.bat", "$root\packaging\Create-Desktop-Shortcut.bat" $dist

# Qt runtime (DLLs + platform/style/image plugins).
$windeployqt = 'G:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe'
& $windeployqt --release --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw `
    --no-compiler-runtime --no-network --skip-plugin-types generic,networkinformation,tls --verbose 0 "$dist\LumaCapture.exe"
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

# FFmpeg (GPLv3 build with libx264): only the libraries LumaCapture loads.
foreach ($lib in 'avcodec-61', 'avformat-61', 'avutil-59', 'swscale-8', 'swresample-5', 'avfilter-10', 'postproc-58') {
    Copy-Item "G:\DevTools\ffmpeg\bin\$lib.dll" $dist
}

# Microsoft Visual C++ runtime, app-local (portable: no installer needed).
$crt = Get-ChildItem 'G:\DevTools\MSVC\Redist' -Directory -Recurse |
       Where-Object { $_.FullName -like '*\x64\Microsoft.VC14*.CRT' } | Select-Object -First 1
if (-not $crt) { throw "MSVC runtime redistributables not found under G:\DevTools\MSVC\Redist" }
Copy-Item "$($crt.FullName)\*.dll" $dist

# Licenses and documentation.
Copy-Item "$root\LICENSE" "$dist\licenses\GPL-3.0.txt"
Copy-Item 'G:\DevTools\CMake\share\cmake-4.4\Licenses\LGPLv3.txt' "$dist\licenses\LGPL-3.0.txt"
Copy-Item "$root\docs\THIRD-PARTY-NOTICES.txt" "$dist\licenses\THIRD-PARTY-NOTICES.txt"
Copy-Item "$root\docs\USER-GUIDE.txt" "$dist\README.txt"

$size = (Get-ChildItem $dist -Recurse -File | Where-Object { $_.FullName -notlike '*\LumaCapture-data\*' } |
         Measure-Object Length -Sum).Sum / 1MB
Write-Host ("Portable build ready: {0}  ({1:N0} MB)" -f $dist, $size)
if ($SkipVerify) { return }

# ---- Verification ------------------------------------------------------------
# 1) Every import of every EXE/DLL resolves to a file in the package or to Windows.
$dumpbin = 'G:\DevTools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe'
$local = @{}
Get-ChildItem $dist -Recurse -Include *.dll, *.exe | ForEach-Object { $local[$_.Name.ToLower()] = $true }
$missing = @()
foreach ($f in Get-ChildItem $dist -Recurse -Include *.dll, *.exe | Where-Object { $_.FullName -notlike '*\LumaCapture-data\*' }) {
    $deps = & $dumpbin /nologo /dependents $f.FullName | Select-String '^\s+(\S+\.dll)\s*$' | ForEach-Object { $_.Matches[0].Groups[1].Value.ToLower() }
    foreach ($d in $deps) {
        if ($d -like 'api-ms-win-*' -or $d -like 'ext-ms-*') { continue }           # Windows API sets
        if ($local.ContainsKey($d)) { continue }
        if (Test-Path "$env:WINDIR\System32\$d") { continue }                     # part of Windows
        $missing += "$($f.Name) -> $d"
    }
}
if ($missing) { throw ("Unresolved DLL imports:`n" + ($missing -join "`n")) }
Write-Host "Dependency check: all imports resolve inside the package or to Windows."

# 2) A copy in a path with spaces starts through the launcher, from another
#    working directory, with a PATH that contains only Windows folders.
$verify = "$root\build\dist verify (spaces)\LumaCapture"
if (Test-Path "$root\build\dist verify (spaces)") { Remove-Item -Recurse -Force "$root\build\dist verify (spaces)" }
New-Item -ItemType Directory -Force $verify | Out-Null
Get-ChildItem -LiteralPath $dist -Force | Where-Object { $_.Name -ne 'LumaCapture-data' } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $verify -Recurse }
$shots = "$root\build\test-output\dist-launch"
if (Test-Path $shots) { Remove-Item -Recurse -Force $shots }
$savedPath = $env:PATH
try {
    $env:PATH = "$env:WINDIR\System32;$env:WINDIR"
    Push-Location $env:WINDIR
    & cmd.exe /c "`"$verify\Launch-LumaCapture.bat`" --ui-snapshot `"$shots`""
    Pop-Location
} finally { $env:PATH = $savedPath }
$deadline = (Get-Date).AddSeconds(60)
while (-not (Test-Path "$shots\layout.png") -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
Start-Sleep 2
if (-not (Test-Path "$shots\main.png")) { throw "Standalone launch via the launcher failed (no UI snapshot produced)" }
Write-Host "Launcher check: the packaged app started from '$verify' (spaces, minimal PATH) and rendered its UI."
Remove-Item -Recurse -Force "$root\build\dist verify (spaces)"
