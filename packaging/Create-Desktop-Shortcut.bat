@echo off
rem Optional: creates a "LumaCapture" shortcut on YOUR desktop after asking.
rem The shortcut points to LumaCapture.exe in this folder. Nothing else is changed.
setlocal
set "HERE=%~dp0"
if not exist "%HERE%LumaCapture.exe" (
    echo LumaCapture.exe was not found in "%HERE%".
    pause
    exit /b 1
)
echo This creates a shortcut named "LumaCapture" on your desktop that starts
echo   "%HERE%LumaCapture.exe"
choice /C YN /M "Create the desktop shortcut"
if errorlevel 2 (
    echo Nothing was changed.
    timeout /t 2 >nul
    exit /b 0
)
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$d=[Environment]::GetFolderPath('Desktop'); $s=(New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $d 'LumaCapture.lnk'));" ^
  "$s.TargetPath=$env:HERE + 'LumaCapture.exe'; $s.WorkingDirectory=$env:HERE; $s.IconLocation=$env:HERE + 'LumaCapture.exe,0';" ^
  "$s.Description='LumaCapture screen recorder'; $s.Save(); Write-Host ('Shortcut created: ' + (Join-Path $d 'LumaCapture.lnk'))"
pause
endlocal
