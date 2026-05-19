; GB2 Portable Launcher
; - Forwards all command-line arguments to GB2.exe
; - Skips re-extraction if already deployed (delete %TEMP%\GB2_runtime to force refresh)
; - Restores caller's working directory so relative --save/--metrics paths resolve correctly
; - Uses ExecWait so terminal waits for GB2 to finish (needed for headless --save/--metrics)

!include "FileFunc.nsh"

Name "GB2"
OutFile "C:\DSSAT48\Tools\gb2\GB2.exe"
RequestExecutionLevel user
SilentInstall silent
SetCompressor /SOLID lzma

Section
    ; Capture caller's working directory BEFORE SetOutPath changes it
    System::Call 'kernel32::GetCurrentDirectory(i 1024, t .r0)'
    System::Call 'kernel32::SetEnvironmentVariable(t "GB2_WORKING_DIR", t r0)'

    ; Skip extraction if already deployed
    IfFileExists "$TEMP\GB2_runtime\GB2.exe" launch

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

    launch:
    ${GetParameters} $R0
    ExecWait '"$TEMP\GB2_runtime\GB2.exe" $R0'
SectionEnd
