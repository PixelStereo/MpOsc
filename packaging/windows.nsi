Unicode true
Name "MpOsc"
Icon "..\assets\MpOsc.ico"
UninstallIcon "..\assets\MpOsc.ico"
OutFile "..\dist\${INSTALLER_NAME}"
InstallDir "$LOCALAPPDATA\Programs\MpOsc"
RequestExecutionLevel user
SetCompressor /SOLID lzma
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\MpOsc"

Section
  SetOutPath "$INSTDIR\app"
  File /r "..\dist\package\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortCut "$SMPROGRAMS\MpOsc.lnk" "$INSTDIR\app\${APP_NAME}.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "MpOsc"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\MpOsc.lnk"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR\app"
  RMDir "$INSTDIR"
SectionEnd
