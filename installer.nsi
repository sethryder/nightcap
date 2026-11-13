; Per-user installer: no admin rights, no UAC prompt.
; Build: make installer VERSION=0.1.0
!define NAME "Nightcap"
!define EXE "nightcap.exe"
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${NAME}"
!ifndef VERSION
  !define VERSION "dev"
!endif

Unicode true
Name "${NAME}"
OutFile "${NAME}-setup.exe"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\${NAME}"
SetCompressor /SOLID lzma

!include MUI2.nsh
!define MUI_ICON "logos/ico/app.ico"
!define MUI_UNICON "logos/ico/app.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section
  ; Ask a running copy to exit so the exe can be replaced (no /F: it closes cleanly and removes its tray icon).
  nsExec::Exec 'taskkill /IM ${EXE}'
  Sleep 500
  SetOutPath "$INSTDIR"
  File "${EXE}"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortcut "$SMPROGRAMS\${NAME}.lnk" "$INSTDIR\${EXE}"
  ; Same value the app's "Start at login" menu item toggles, so the tray tick matches.
  WriteRegStr HKCU "${RUN_KEY}" "${NAME}" '"$INSTDIR\${EXE}"'
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${NAME}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${EXE}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "Seth Ryder"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  nsExec::Exec 'taskkill /IM ${EXE}'
  Sleep 500
  Delete "$INSTDIR\${EXE}"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${NAME}.lnk"
  DeleteRegValue HKCU "${RUN_KEY}" "${NAME}"
  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\${NAME}"  ; the app's saved delay
SectionEnd
