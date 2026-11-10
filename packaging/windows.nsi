Unicode true
Name "MP_OSC"
OutFile "..\dist\MP_OSC-Windows-x64-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\MP_OSC"
RequestExecutionLevel user
SetCompressor /SOLID lzma
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\MP_OSC"

Section
  SetOutPath "$INSTDIR\app"
  File /r "..\dist\package\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortCut "$SMPROGRAMS\MP_OSC.lnk" "$INSTDIR\app\MP_OSC.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "MP_OSC"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\MP_OSC.lnk"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR\app"
  RMDir "$INSTDIR"
SectionEnd
