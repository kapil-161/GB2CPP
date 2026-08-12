; GB2 Portable Launcher
; - Forwards all command-line arguments to GB2.exe
; - Keys the extracted runtime on the CONTENT HASH of the bundled GB2.exe, not a
;   version string. Two exe files with different binaries always extract to
;   different folders, so a machine that already ran an older build can never
;   reuse its cached runtime for a newer one (the bug where a 2.0.60 exe opened
;   a stale 2.0.40 on someone else's laptop).
; - Extracts into a per-content subfolder, so an old copy that happens to still
;   be running cannot lock/block extraction of the new one.
; - Restores caller's working directory so relative --save/--metrics paths resolve
; - Uses ExecWait so terminal waits for GB2 to finish (needed for headless --save)

!include "FileFunc.nsh"

; Version injected by build_and_deploy.bat via /DVERSION=x.y.z (display/debug only)
!ifndef VERSION
  !define VERSION "unknown"
!endif
; Content hash of the bundled GB2.exe, injected via /DPAYLOADHASH=<md5>.
; This — not VERSION — is what identifies the runtime, so a rebuilt or
; re-shared binary is never confused with a previously cached one.
!ifndef PAYLOADHASH
  !define PAYLOADHASH "nohash"
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

    ; Per-content runtime folder. A different bundled binary → different hash →
    ; different folder → guaranteed fresh extraction (never a stale reuse).
    StrCpy $8 "$TEMP\GB2_runtime\${PAYLOADHASH}"

    ; Skip extraction only if THIS exact binary is already fully extracted.
    ; The ready marker is written last, so its presence means extraction finished.
    IfFileExists "$8\ready.marker" launch

    ; New/changed binary: clear ALL previously cached runtimes so stale versions
    ; can't linger and temp doesn't accumulate. This is best-effort — if an old
    ; build is still running, Windows keeps its locked files (harmless) and we
    ; still extract cleanly into our own per-content folder below.
    RMDir /r "$TEMP\GB2_runtime"

    SetOutPath "$8"
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

    SetOutPath "$8\platforms"
    File "manual_deployment\platforms\qwindows.dll"

    SetOutPath "$8\resources"
    File "manual_deployment\resources\final.ico"
    File "manual_deployment\resources\final.png"

    ; Write the ready marker LAST so a future launch only skips re-extraction
    ; once every file above is present.
    SetOutPath "$8"
    FileOpen $0 "$8\ready.marker" w
    FileWrite $0 "${VERSION}"
    FileClose $0

    launch:
    ${GetParameters} $R0
    ExecWait '"$8\GB2.exe" $R0'
SectionEnd
