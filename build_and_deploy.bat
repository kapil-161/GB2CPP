@echo off
REM Suppress all output except errors for final build
if "%1"=="quiet" (
    @echo off >nul 2>&1
    set QUIET_MODE=1
) else (
    echo ========================================
    echo GB2 Build and Deployment Script
    echo ========================================
    echo.
)

REM Set Qt license bypass
set QTFRAMEWORK_BYPASS_LICENSE_CHECK=1

REM Code signing (optional): set one of these BEFORE running this script to
REM sign the built exe. Unset = build stays unsigned, exactly as before.
REM   GB2_SIGN_CERT           path to a .pfx certificate file
REM   GB2_SIGN_PASS           password for that .pfx (paired with GB2_SIGN_CERT)
REM   GB2_SIGN_THUMBPRINT     SHA1 thumbprint of a cert in the Windows cert
REM                           store (use this for an EV cert on a hardware
REM                           token, which can't be exported as a .pfx)
REM   GB2_SIGN_TIMESTAMP_URL  optional, defaults to DigiCert's RFC3161 server
REM Requires signtool.exe (from the Windows SDK) on PATH or in its usual
REM Windows Kits install location.

REM Set paths - These will be auto-detected or you can override them
set PROJECT_DIR=%~dp0

REM GB2 is linked against a statically-built Qt (no Qt6*.dll, no wrapper/
REM extraction step at build or run time — see CMakeLists.txt's GB2_STATIC_QT
REM option). This is a one-time local build separate from the normal Qt
REM installer; see C:\Qt6Static for the qtbase+qtcharts source/build trees
REM used to produce it.
set STATIC_QT_DIR=C:\Qt6Static\install
set CMAKE_PATH=
set MINGW_PATH=
set NINJA_PATH=

if not exist "%STATIC_QT_DIR%\lib\cmake\Qt6" (
    echo ERROR: Static Qt install not found at %STATIC_QT_DIR%
    echo This build requires a Qt6 built with -static ^(qtbase + qtcharts,
    echo matching the MinGW toolchain below^). It is not part of the normal
    echo Qt online installer and must be built from source once.
    pause
    exit /b 1
)

REM Auto-detect CMake
if exist "C:\Qt\Tools\CMake\bin\cmake.exe" (
    set CMAKE_PATH=C:\Qt\Tools\CMake\bin\cmake.exe
) else if exist "C:\Qt\Tools\CMake_64\bin\cmake.exe" (
    set CMAKE_PATH=C:\Qt\Tools\CMake_64\bin\cmake.exe
) else if exist "C:\Program Files\CMake\bin\cmake.exe" (
    set CMAKE_PATH=C:\Program Files\CMake\bin\cmake.exe
) else (
    set CMAKE_PATH=cmake.exe
)

REM Auto-detect Ninja (used as the CMake generator for the static build)
if exist "C:\Qt\Tools\Ninja\ninja.exe" (
    set NINJA_PATH=C:\Qt\Tools\Ninja\ninja.exe
) else (
    set NINJA_PATH=ninja.exe
)

REM Auto-detect MinGW - must be the SAME toolchain the static Qt was built
REM with (mingw1310_64), since static Qt's .a archives are compiler-ABI
REM specific.
for %%i in (mingw1310_64 mingw1120_64 mingw1020_64) do (
    if exist "C:\Qt\Tools\%%i\bin" (
        set MINGW_PATH=C:\Qt\Tools\%%i\bin
        goto :found_mingw
    )
)

REM Fallback
set MINGW_PATH=C:\Qt\Tools\mingw1310_64\bin

:found_mingw
echo Found MinGW at: %MINGW_PATH%

if not exist "%CMAKE_PATH%" (
    echo ERROR: CMake not found: %CMAKE_PATH%
    echo Please update CMAKE_PATH in this script
    pause
    exit /b 1
)

if not exist "%MINGW_PATH%" (
    echo ERROR: MinGW not found: %MINGW_PATH%
    echo Please update MINGW_PATH in this script
    pause
    exit /b 1
)

echo Step 1: Cleaning previous build...
cd /d "%PROJECT_DIR%"

REM Clear stale single-instance lock so first launch after build is never blocked
if exist "%TEMP%\GB2.instance.lock" (
    echo Clearing stale instance lock: %TEMP%\GB2.instance.lock
    del /f /q "%TEMP%\GB2.instance.lock"
)

REM Force kill any processes using the build directory
taskkill /f /im cmake.exe 2>nul
taskkill /f /im GB2.exe 2>nul

REM Wait and try to remove directory
timeout /t 2 /nobreak >nul
if exist build_win (
    echo Removing existing build directory...
    rmdir /s /q build_win 2>nul
    if exist build_win (
        echo WARNING: Could not remove build_win directory completely
        echo Some files may be in use. Continuing anyway...
    )
)

mkdir build_win
cd build_win

echo.
if not defined QUIET_MODE echo Step 2: Configuring with CMake ^(static Qt^)...

REM Add MinGW to PATH temporarily
set PATH=%MINGW_PATH%;%PATH%

REM Verify compiler exists
if not exist "%MINGW_PATH%\g++.exe" (
    echo ERROR: g++.exe not found at %MINGW_PATH%\g++.exe
    pause
    exit /b 1
)

if not defined QUIET_MODE (
    echo Using compiler: %MINGW_PATH%\g++.exe
    echo Using static Qt path: %STATIC_QT_DIR%
)

if defined QUIET_MODE (
    "%CMAKE_PATH%" .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DGB2_STATIC_QT=ON -DCMAKE_CXX_COMPILER="%MINGW_PATH%\g++.exe" -DCMAKE_C_COMPILER="%MINGW_PATH%\gcc.exe" -DCMAKE_MAKE_PROGRAM="%NINJA_PATH%" -DCMAKE_PREFIX_PATH="%STATIC_QT_DIR%" >nul 2>&1
) else (
    "%CMAKE_PATH%" .. -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DGB2_STATIC_QT=ON ^
      -DCMAKE_CXX_COMPILER="%MINGW_PATH%\g++.exe" ^
      -DCMAKE_C_COMPILER="%MINGW_PATH%\gcc.exe" ^
      -DCMAKE_MAKE_PROGRAM="%NINJA_PATH%" ^
      -DCMAKE_PREFIX_PATH="%STATIC_QT_DIR%"
)

if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed!
    pause
    exit /b 1
)

if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 3: Building application...
if defined QUIET_MODE (
    "%CMAKE_PATH%" --build . >nul 2>&1
) else (
    "%CMAKE_PATH%" --build .
)

if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)

if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 4: Creating deployment folder...
cd /d "%PROJECT_DIR%"
if exist manual_deployment rmdir /s /q manual_deployment
mkdir manual_deployment

REM Copy executable - statically linked, no Qt6*.dll / MinGW runtime DLLs needed
if exist build_win\bin\GB2.exe (
    copy build_win\bin\GB2.exe manual_deployment\
) else (
    echo ERROR: GB2.exe not found in build_win\bin\
    echo Build may have failed
    pause
    exit /b 1
)

call :sign_exe "%PROJECT_DIR%manual_deployment\GB2.exe"

REM Small resources folder (icons) - not embedded in the exe, loaded by
REM relative path at runtime (see main.cpp setupApplicationIcon). Just data
REM files sitting next to the exe, not a wrapper/extraction step.
if exist resources xcopy /E /I resources manual_deployment\resources

REM Copy license text so it ships alongside the exe
if exist LICENSE copy /Y LICENSE manual_deployment\LICENSE.txt >nul

if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 5: Deploying directly to C:\DSSAT48\Tools\GB2\...
REM No windeployqt, no NSIS wrapper: GB2.exe is fully self-contained (static
REM Qt + static MinGW runtime), so deployment is just "copy the exe and its
REM small resources folder to where the Start Menu shortcut already points."
if exist "C:\DSSAT48\Tools\GB2" (
    del /f /q "C:\DSSAT48\Tools\GB2\*.*" 2>nul
    for /d %%d in ("C:\DSSAT48\Tools\GB2\*") do rmdir /s /q "%%d" 2>nul
) else (
    mkdir "C:\DSSAT48\Tools\GB2"
)
xcopy /E /I /Y manual_deployment\* "C:\DSSAT48\Tools\GB2\" >nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: Deployment to C:\DSSAT48\Tools\GB2 failed!
    pause
    exit /b 1
)
if not defined QUIET_MODE echo SUCCESS: Deployed to C:\DSSAT48\Tools\GB2\GB2.exe

if not defined QUIET_MODE (
    echo.
    echo ========================================
    echo SUCCESS: Build and deployment complete!
    echo ========================================
    echo.
    echo Deployment folder: %PROJECT_DIR%manual_deployment
    echo Installed at: C:\DSSAT48\Tools\GB2\GB2.exe
    echo ========================================
    pause
) else (
    echo Build complete. Deployment folder: %PROJECT_DIR%manual_deployment
)

exit /b 0

REM ============================================================
REM :sign_exe "path\to\file.exe"
REM No-op unless GB2_SIGN_CERT or GB2_SIGN_THUMBPRINT is set (see top of file).
REM ============================================================
:sign_exe
if not defined GB2_SIGN_CERT if not defined GB2_SIGN_THUMBPRINT (
    if not defined QUIET_MODE echo Skipping code signing for %~1 ^(set GB2_SIGN_CERT or GB2_SIGN_THUMBPRINT to enable^)
    exit /b 0
)

if not defined SIGNTOOL_PATH (
    where signtool.exe >nul 2>&1
    if not errorlevel 1 (
        set SIGNTOOL_PATH=signtool.exe
    ) else (
        for /f "delims=" %%s in ('dir /b /s "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" 2^>nul') do if not defined SIGNTOOL_PATH set SIGNTOOL_PATH=%%s
    )
)
if not defined SIGNTOOL_PATH (
    echo WARNING: signtool.exe not found ^(install the Windows SDK^) - cannot sign %~1
    exit /b 0
)

if not defined GB2_SIGN_TIMESTAMP_URL set GB2_SIGN_TIMESTAMP_URL=http://timestamp.digicert.com

if not defined QUIET_MODE echo Signing %~1 ...
if defined GB2_SIGN_THUMBPRINT (
    "%SIGNTOOL_PATH%" sign /sha1 %GB2_SIGN_THUMBPRINT% /fd SHA256 /tr "%GB2_SIGN_TIMESTAMP_URL%" /td SHA256 "%~1"
) else (
    "%SIGNTOOL_PATH%" sign /f "%GB2_SIGN_CERT%" /p "%GB2_SIGN_PASS%" /fd SHA256 /tr "%GB2_SIGN_TIMESTAMP_URL%" /td SHA256 "%~1"
)
if errorlevel 1 (
    echo WARNING: Signing failed for %~1
) else (
    if not defined QUIET_MODE echo Signed: %~1
)
exit /b 0
