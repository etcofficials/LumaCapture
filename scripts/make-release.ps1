<#
.SYNOPSIS
  Creates the release files from the portable build in dist\LumaCapture:
      G:\LumaCapture\release\LumaCapture-v<version>-Portable-Windows-x64.zip
      G:\LumaCapture\release\LumaCapture-v<version>-Setup.exe       (NSIS, per-user installer)
      G:\LumaCapture\release\SHA256SUMS.txt
  ZIP layout: LumaCapture\ (the app folder) + README-FIRST.txt.
  The local LumaCapture-data folder (settings, history, logs) is never included.
  Run scripts\package.ps1 first. Everything stays on G:.

  Usage: powershell -File G:\LumaCapture\scripts\make-release.ps1 -Version 2.0.0 [-SkipInstaller]
#>
param([Parameter(Mandatory)][string]$Version, [switch]$SkipInstaller)
$ErrorActionPreference = 'Stop'

$root  = 'G:\LumaCapture'
$dist  = "$root\dist\LumaCapture"
$rel   = "$root\release"
$stage = "$rel\staging"
$zip   = "$rel\LumaCapture-v$Version-Portable-Windows-x64.zip"
$setup = "$rel\LumaCapture-v$Version-Setup.exe"
$makensis = 'G:\DevTools\nsis\makensis.exe'
$env:TEMP = "$root\build\tmp"; $env:TMP = $env:TEMP
New-Item -ItemType Directory -Force $rel, $env:TEMP | Out-Null

if (-not (Test-Path "$dist\LumaCapture.exe")) { throw "Portable build not found - run scripts\package.ps1 first" }
$exeVersion = (Get-Item "$dist\LumaCapture.exe").VersionInfo.ProductVersion
if ($exeVersion -ne $Version) { throw "LumaCapture.exe is version $exeVersion, not $Version" }

# ---- Portable ZIP --------------------------------------------------------------
if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force "$stage\LumaCapture" | Out-Null
Get-ChildItem -LiteralPath $dist -Force | Where-Object { $_.Name -ne 'LumaCapture-data' } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination "$stage\LumaCapture" -Recurse }
# README-FIRST with Windows line endings so it opens cleanly in Notepad.
$text = [IO.File]::ReadAllText("$root\packaging\README-FIRST.txt") -replace "`r?`n", "`r`n"
[IO.File]::WriteAllText("$stage\README-FIRST.txt", $text, (New-Object Text.UTF8Encoding $false))
if (Test-Path $zip) { Remove-Item -LiteralPath $zip }
# Windows' bsdtar writes standard ZIPs (forward-slash entry names, deflate).
& "$env:WINDIR\System32\tar.exe" -a -c -f $zip -C $stage LumaCapture README-FIRST.txt
if ($LASTEXITCODE -ne 0) { throw "creating the ZIP failed" }
Remove-Item -LiteralPath $stage -Recurse -Force
Write-Host ("Portable ZIP: {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))

# ---- Installer -----------------------------------------------------------------
$files = @($zip)
if (-not $SkipInstaller) {
    if (-not (Test-Path $makensis)) { throw "NSIS not found at $makensis" }
    if (Test-Path $setup) { Remove-Item -LiteralPath $setup }
    & $makensis /V2 "/DVERSION=$Version" "/DDIST=$dist" "/DOUTFILE=$setup" "$root\installer\LumaCapture.nsi"
    if ($LASTEXITCODE -ne 0) { throw "makensis failed" }
    Write-Host ("Installer:    {0} ({1:N1} MB)" -f $setup, ((Get-Item $setup).Length / 1MB))
    $files += $setup
}

# ---- Checksums -----------------------------------------------------------------
$sums = foreach ($f in $files) { "{0}  {1}" -f (Get-FileHash $f -Algorithm SHA256).Hash.ToLower(), (Split-Path $f -Leaf) }
[IO.File]::WriteAllLines("$rel\SHA256SUMS.txt", [string[]]$sums)
$sums | ForEach-Object { Write-Host $_ }
