<#
.SYNOPSIS
  Creates the distributable ZIP from the portable build in dist\LumaCapture:
      G:\LumaCapture\release\LumaCapture-v<version>-Windows-x64.zip
  ZIP layout: LumaCapture\ (the app folder) + README-FIRST.txt.
  The local LumaCapture-data folder (settings, history, logs) is never included.
  Run scripts\package.ps1 first. Everything stays on G:.

  Usage: powershell -File G:\LumaCapture\scripts\make-release.ps1 -Version 1.0.0
#>
param([Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'

$root  = 'G:\LumaCapture'
$dist  = "$root\dist\LumaCapture"
$rel   = "$root\release"
$stage = "$rel\staging"
$zip   = "$rel\LumaCapture-v$Version-Windows-x64.zip"
$env:TEMP = "$root\build\tmp"; $env:TMP = $env:TEMP

if (-not (Test-Path "$dist\LumaCapture.exe")) { throw "Portable build not found - run scripts\package.ps1 first" }
$exeVersion = (Get-Item "$dist\LumaCapture.exe").VersionInfo.ProductVersion
if ($exeVersion -ne $Version) { throw "LumaCapture.exe is version $exeVersion, not $Version" }

if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force "$stage\LumaCapture" | Out-Null
Get-ChildItem -LiteralPath $dist -Force | Where-Object { $_.Name -ne 'LumaCapture-data' } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination "$stage\LumaCapture" -Recurse }

# README-FIRST with Windows line endings so it opens cleanly in Notepad.
$text = [IO.File]::ReadAllText("$root\packaging\README-FIRST.txt") -replace "`r?`n", "`r`n"
[IO.File]::WriteAllText("$stage\README-FIRST.txt", $text, (New-Object Text.UTF8Encoding $false))

# Windows' bsdtar writes standard ZIPs (forward-slash entry names, deflate).
if (Test-Path $zip) { Remove-Item -LiteralPath $zip }
& "$env:WINDIR\System32\tar.exe" -a -c -f $zip -C $stage LumaCapture README-FIRST.txt
if ($LASTEXITCODE -ne 0) { throw "creating the ZIP failed" }
Remove-Item -LiteralPath $stage -Recurse -Force

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash
Write-Host ("Release ZIP: {0}  ({1:N1} MB)`nSHA-256: {2}" -f $zip, ((Get-Item $zip).Length / 1MB), $hash)
