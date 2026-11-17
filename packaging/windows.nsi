; Windows installer shared by the Pixel Stereo applications.
; The user chooses at install time: for all users (the default, needs administrator rights, Program Files) or for the
; current user only (no administrator rights, %LOCALAPPDATA%\Programs). Silent installs: /S /AllUsers or /S /CurrentUser.
;
;   makensis /DPRODUCT=Name /DAPP_VERSION=1.2.3+4 "/DAPP_EXE=Name 1.2.3+4.exe" /DPAYLOAD=C:\dir /DOUTPUT=C:\out\Setup.exe [/DICON=app.ico] windows.nsi
Unicode true
!ifndef PRODUCT
  !error "PRODUCT is required"
!endif
!ifndef APP_VERSION
  !error "APP_VERSION is required"
!endif
!ifndef APP_EXE
  !error "APP_EXE is required"
!endif
!ifndef PAYLOAD
  !error "PAYLOAD is required"
!endif
!ifndef OUTPUT
  !error "OUTPUT is required"
!endif

Name "${PRODUCT} ${APP_VERSION}"
OutFile "${OUTPUT}"
SetCompressor /SOLID lzma
!ifdef ICON
  !define MUI_ICON "${ICON}"
  !define MUI_UNICON "${ICON}"
!endif

!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_INSTALLMODE_64_BIT
!define MULTIUSER_INSTALLMODE_INSTDIR "${PRODUCT}"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "Software\${PRODUCT}"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME "InstallDir"
!include MultiUser.nsh

!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT}"

; The install mode picks the default folder; a folder given on the command line (/D=<dir>, last) wins over it
InstallDir "pxs-default-folder"
Var RequestedDir
Function .onInit
  StrCpy $RequestedDir $INSTDIR
  !insertmacro MULTIUSER_INIT
  ${If} $RequestedDir != "pxs-default-folder"
    StrCpy $INSTDIR $RequestedDir
  ${EndIf}
FunctionEnd

Function un.onInit
  !insertmacro MULTIUSER_UNINIT
FunctionEnd

Section
  ; The executable carries the version: an upgrade must not leave the previous one beside the new one
  RMDir /r "$INSTDIR\app"
  SetOutPath "$INSTDIR\app"
  File /r "${PAYLOAD}\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortCut "$SMPROGRAMS\${PRODUCT}.lnk" "$INSTDIR\app\${APP_EXE}"
  WriteRegStr SHCTX "Software\${PRODUCT}" "InstallDir" "$INSTDIR"
  WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT} ${APP_VERSION}"
  WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr SHCTX "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr SHCTX "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe" /$MultiUser.InstallMode'
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\${PRODUCT}.lnk"
  DeleteRegKey SHCTX "${UNINSTALL_KEY}"
  DeleteRegKey SHCTX "Software\${PRODUCT}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR\app"
  RMDir "$INSTDIR"
SectionEnd
