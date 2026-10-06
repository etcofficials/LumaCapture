; LumaCapture installer (NSIS 3, Modern UI 2). Built by scripts\make-release.ps1:
;   makensis /DVERSION=2.0.0 /DDIST=G:\LumaCapture\dist\LumaCapture /DOUTFILE=...\LumaCapture-v2.0.0-Setup.exe installer\LumaCapture.nsi
;
; Per-user install (no administrator rights): files under the chosen folder
; (default %LOCALAPPDATA%\Programs\LumaCapture), a Start Menu shortcut, an
; optional Desktop shortcut and an entry in "Apps & features". The uninstaller
; keeps the user's settings/library unless asked, and never touches recordings.

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user

!ifndef VERSION
  !error "Pass /DVERSION=x.y.z"
!endif
!ifndef DIST
  !error "Pass /DDIST=<portable folder>"
!endif
!ifndef OUTFILE
  !define OUTFILE "LumaCapture-v${VERSION}-Setup.exe"
!endif

!define APPNAME "LumaCapture"
!define PUBLISHER "etcofficials"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\LumaCapture"
!define MUTEX "Local\LumaCapture.SingleInstance"

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\LumaCapture"
InstallDirRegKey HKCU "${UNINSTKEY}" "InstallLocation"
BrandingText "${APPNAME} ${VERSION}"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APPNAME} installer"
VIAddVersionKey "LegalCopyright" "GPL-3.0"

!define MUI_ICON "${DIST}\..\..\resources\lumacapture.ico"
!define MUI_UNICON "${DIST}\..\..\resources\lumacapture.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\LumaCapture.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start LumaCapture"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\README.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Show the user guide"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${DIST}\licenses\GPL-3.0.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; Refuses to continue while LumaCapture is running (it would hold its files open).
!macro CHECK_NOT_RUNNING
  System::Call 'kernel32::OpenMutexW(i 0x00100000, i 0, w "${MUTEX}") p .r0'
  ${If} $0 P<> 0
    System::Call 'kernel32::CloseHandle(p r0)'
    MessageBox MB_OK|MB_ICONEXCLAMATION "LumaCapture is running. Please close it (also from the notification area) and try again."
    Abort
  ${EndIf}
!macroend

Function .onInit
  !insertmacro CHECK_NOT_RUNNING
FunctionEnd

Function un.onInit
  !insertmacro CHECK_NOT_RUNNING
FunctionEnd

Section "LumaCapture (required)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  ; The portable folder, without any user data that may exist next to it.
  File /r /x "LumaCapture-data" /x "Launch-LumaCapture.bat" /x "Create-Desktop-Shortcut.bat" "${DIST}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\LumaCapture"
  CreateShortcut "$SMPROGRAMS\LumaCapture\LumaCapture.lnk" "$INSTDIR\LumaCapture.exe" "" "$INSTDIR\LumaCapture.exe" 0 SW_SHOWNORMAL "" "Lightweight screen recorder"
  CreateShortcut "$SMPROGRAMS\LumaCapture\Uninstall LumaCapture.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\LumaCapture.exe,0"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegStr HKCU "${UNINSTKEY}" "URLInfoAbout" "https://github.com/etcofficials/LumaCapture"
  WriteRegStr HKCU "${UNINSTKEY}" "HelpLink" "https://github.com/etcofficials/LumaCapture/issues"
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2 ; KB, as "Apps & features" expects
  WriteRegDWORD HKCU "${UNINSTKEY}" "EstimatedSize" "$0"
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\LumaCapture.lnk" "$INSTDIR\LumaCapture.exe" "" "$INSTDIR\LumaCapture.exe" 0 SW_SHOWNORMAL "" "Lightweight screen recorder"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "The application, its runtime libraries (Qt, FFmpeg, Microsoft C++ runtime), licenses and user guide, plus a Start Menu shortcut."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "A LumaCapture shortcut on the Desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  Delete "$DESKTOP\LumaCapture.lnk"
  Delete "$SMPROGRAMS\LumaCapture\LumaCapture.lnk"
  Delete "$SMPROGRAMS\LumaCapture\Uninstall LumaCapture.lnk"
  RMDir "$SMPROGRAMS\LumaCapture"

  ; Program files only (what the installer put there).
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\iconengines"
  RMDir /r "$INSTDIR\licenses"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\LumaCapture.exe"
  Delete "$INSTDIR\luma-bench.exe"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\Uninstall.exe"

  ; Settings, library, logs: kept unless the user agrees (silent uninstall keeps them).
  ${If} ${FileExists} "$INSTDIR\LumaCapture-data\*.*"
    MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Also delete your LumaCapture settings, library list and logs?$\n$\n$INSTDIR\LumaCapture-data$\n$\nYour recordings are never deleted." /SD IDNO IDNO keepData
    RMDir /r "$INSTDIR\LumaCapture-data"
  keepData:
  ${EndIf}
  RMDir "$INSTDIR" ; only if empty

  DeleteRegKey HKCU "${UNINSTKEY}"
SectionEnd
