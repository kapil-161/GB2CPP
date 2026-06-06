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

REM Set paths - These will be auto-detected or you can override them
set PROJECT_DIR=%~dp0

REM Try to auto-detect Qt installation
set QT_DIR=
set CMAKE_PATH=
set MINGW_PATH=

REM Check common Qt installation locations
for %%i in (6.8.1 6.8.2 6.9.1 6.11.1 6.11.0 6.7.1 6.6.1) do (
    if exist "C:\Qt\%%i\mingw_64\" (
        set QT_DIR=C:\Qt\%%i\mingw_64
        echo Found Qt at: C:\Qt\%%i\mingw_64
        goto :found_qt
    )
)

echo ERROR: Qt not found. Please install Qt 6.x with MinGW to C:\Qt (https://www.qt.io/download-qt-installer)
pause
exit /b 1

:found_qt

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

REM Auto-detect MinGW
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

REM Check if paths exist
if not exist "%QT_DIR%" (
    echo ERROR: Qt directory not found: %QT_DIR%
    echo Please update QT_DIR in this script
    pause
    exit /b 1
)

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

REM Clear any previously extracted runtime so the updated exe is used on next launch
if exist "%TEMP%\GB2_runtime" (
    echo Clearing cached runtime: %TEMP%\GB2_runtime
    rmdir /s /q "%TEMP%\GB2_runtime"
)

REM Force kill any processes using the build directory
taskkill /f /im cmake.exe 2>nul
taskkill /f /im mingw32-make.exe 2>nul
taskkill /f /im g++.exe 2>nul

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
if not defined QUIET_MODE echo Step 2: Configuring with CMake...

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
    echo Using Qt path: %QT_DIR%
)

if defined QUIET_MODE (
    "%CMAKE_PATH%" .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="%MINGW_PATH%\g++.exe" -DCMAKE_C_COMPILER="%MINGW_PATH%\gcc.exe" -DCMAKE_MAKE_PROGRAM="%MINGW_PATH%\mingw32-make.exe" -DCMAKE_PREFIX_PATH="%QT_DIR%" >nul 2>&1
) else (
    "%CMAKE_PATH%" .. -G "MinGW Makefiles" ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_CXX_COMPILER="%MINGW_PATH%\g++.exe" ^
      -DCMAKE_C_COMPILER="%MINGW_PATH%\gcc.exe" ^
      -DCMAKE_MAKE_PROGRAM="%MINGW_PATH%\mingw32-make.exe" ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%"
)

if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed!
    pause
    exit /b 1
)

if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 3: Building application...
if defined QUIET_MODE (
    "%MINGW_PATH%\mingw32-make.exe" >nul 2>&1
) else (
    "%MINGW_PATH%\mingw32-make.exe"
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

REM Copy executable
if exist build_win\bin\GB2.exe (
    copy build_win\bin\GB2.exe manual_deployment\
) else (
    echo ERROR: GB2.exe not found in build_win\bin\
    echo Build may have failed
    pause
    exit /b 1
)

REM Copy resources
if exist resources xcopy /E /I resources manual_deployment\resources

if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 5: Deploying Qt6 dependencies...
if defined QUIET_MODE (
    "%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw manual_deployment\GB2.exe >nul 2>&1
) else (
    "%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw manual_deployment\GB2.exe
)

if %ERRORLEVEL% neq 0 (
    echo ERROR: windeployqt failed!
    pause
    exit /b 1
)

if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 6: Removing unnecessary files and folders...
REM Remove unwanted plugin folders (KEEP platforms folder - it's essential!)
if exist manual_deployment\generic rmdir /s /q manual_deployment\generic
if exist manual_deployment\iconengines rmdir /s /q manual_deployment\iconengines
if exist manual_deployment\imageformats rmdir /s /q manual_deployment\imageformats
if exist manual_deployment\networkinformation rmdir /s /q manual_deployment\networkinformation
if exist manual_deployment\styles rmdir /s /q manual_deployment\styles
if exist manual_deployment\tls rmdir /s /q manual_deployment\tls

REM Remove unwanted DLL files (KEEP libgcc_s_seh-1.dll and libwinpthread-1.dll - they're needed!)
if exist manual_deployment\Qt6Network.dll del manual_deployment\Qt6Network.dll
if exist manual_deployment\Qt6Svg.dll del manual_deployment\Qt6Svg.dll
if exist manual_deployment\D3Dcompiler_47.dll del manual_deployment\D3Dcompiler_47.dll
if exist manual_deployment\opengl32sw.dll del manual_deployment\opengl32sw.dll

if not defined QUIET_MODE echo Removed unnecessary files to minimize deployment size
if not defined QUIET_MODE echo KEPT: platforms folder, libgcc/libwinpthread

if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 7: Building single portable GB2.exe with NSIS...
cd /d "%PROJECT_DIR%"

REM Auto-detect NSIS
set NSIS_PATH=
if exist "C:\Program Files\NSIS\makensis.exe" set NSIS_PATH=C:\Program Files\NSIS\makensis.exe
if exist "C:\Program Files (x86)\NSIS\makensis.exe" set NSIS_PATH=C:\Program Files (x86)\NSIS\makensis.exe

if not defined NSIS_PATH (
    echo WARNING: NSIS not found - skipping portable exe build
    echo Install NSIS from https://nsis.sourceforge.io to enable single-exe packaging
    goto :skip_nsis
)

REM Extract version from version_generated.h and pass to NSIS
set GB2_VERSION=unknown
for /f "tokens=3" %%v in ('findstr /c:"#define GB2_VERSION " "%PROJECT_DIR%include\version_generated.h"') do set GB2_VERSION=%%v
set GB2_VERSION=%GB2_VERSION:"=%
if not defined QUIET_MODE echo Packaging version: %GB2_VERSION%

"%NSIS_PATH%" /DVERSION=%GB2_VERSION% gb2_launcher.nsi
if %ERRORLEVEL% neq 0 (
    echo ERROR: NSIS packaging failed!
    goto :skip_nsis
)
if not defined QUIET_MODE echo SUCCESS: Single portable exe saved to C:\DSSAT48\Tools\gb2\GB2.exe

:skip_nsis
if not defined QUIET_MODE (
    echo.
    echo ========================================
    echo SUCCESS: Build and deployment complete!
    echo ========================================
    echo.
    echo Deployment folder: %PROJECT_DIR%manual_deployment
    echo Portable single exe: C:\DSSAT48\Tools\gb2\GB2.exe
    echo ========================================
    pause
) else (
    echo Build complete. Deployment folder: %PROJECT_DIR%manual_deployment
)