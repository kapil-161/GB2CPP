; GB2 Portable Launcher
; - Forwards all command-line arguments to GB2.exe
; - Skips re-extraction if this exact version is already deployed
; - Forces re-extraction when a new version is bundled (version marker mismatch)
; - Restores caller's working directory so relative --save/--metrics paths resolve correctly
; - Uses ExecWait so terminal waits for GB2 to finish (needed for headless --save/--metrics)

!include "FileFunc.nsh"

; Version injected by build_and_deploy.bat via /DVERSION=x.y.z
!ifndef VERSION
  !define VERSION "unknown"
!endif

Name "GB2"
OutFile "C:\DSSAT48\Tools\gb2\GB2.exe"
; Give the packaged launcher exe the GB2 application icon (otherwise NSIS uses
; its generic default icon). Path is relative to this .nsi file.
Icon "manual_deployment\resources\final.ico"
RequestExecutionLevel user
SilentInstall silent
SetCompressor /SOLID lzma

Section
    ; Capture caller's working directory BEFORE SetOutPath changes it
    System::Call 'kernel32::GetCurrentDirectory(i 1024, t .r0)'
    System::Call 'kernel32::SetEnvironmentVariable(t "GB2_WORKING_DIR", t r0)'

    ; Skip extraction only if this exact version+hash marker exists
    IfFileExists "$TEMP\GB2_runtime\version_${VERSION}.marker" launch

    ; Remove stale runtime (any old version/hash) before extracting new one
    RMDir /r "$TEMP\GB2_runtime"

    SetOutPath "$TEMP\GB2_runtime"
    File "manual_deployment\GB2.exe"
    File "manual_deployment\libgcc_s_seh-1.dll"
    File "manual_deployment\libstdc++-6.dll"
    File "manual_deployment\libwinpthread-1.dll"
    File "manual_deployment\Qt6Charts.dll"
    File "manual_deployment\Qt6Core.dll"
    File "manual_deployment\Qt6Gui.dll"
    File "manual_deployment\Qt6OpenGL.dll"
    File "manual_deployment\Qt6OpenGLWidgets.dll"
    File "manual_deployment\Qt6Widgets.dll"

    SetOutPath "$TEMP\GB2_runtime\platforms"
    File "manual_deployment\platforms\qwindows.dll"

    SetOutPath "$TEMP\GB2_runtime\resources"
    File "manual_deployment\resources\final.ico"
    File "manual_deployment\resources\final.png"

    ; Write version marker so future launches skip re-extraction
    SetOutPath "$TEMP\GB2_runtime"
    FileOpen $0 "$TEMP\GB2_runtime\version_${VERSION}.marker" w
    FileClose $0

    launch:
    ${GetParameters} $R0
    ExecWait '"$TEMP\GB2_runtime\GB2.exe" $R0'
SectionEnd
