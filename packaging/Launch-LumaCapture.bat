@echo off
rem LumaCapture launcher - starts LumaCapture.exe from the folder this file is in.
rem Works from any current directory and with spaces in the path. Needs no
rem administrator rights and changes nothing on the system.
setlocal
set "HERE=%~dp0"
if not exist "%HERE%LumaCapture.exe" (
    echo.
    echo  LumaCapture.exe was not found next to this launcher:
    echo    "%HERE%"
    echo.
    echo  Keep the whole LumaCapture folder together ^(the .exe, its DLLs and
    echo  the platforms, styles and imageformats folders^) and try again.
    echo.
    pause
    exit /b 1
)
rem "start" returns immediately, so this console window closes right away.
start "" /D "%HERE%" "%HERE%LumaCapture.exe" %*
endlocal
exit /b 0
