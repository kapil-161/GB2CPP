@echo off
setlocal
REM Setup environment for headless Qt rendering
set PATH=C:\Qt\Tools\mingw1310_64\bin;C:\usr\bin;%PATH%
set QTFRAMEWORK_BYPASS_LICENSE_CHECK=1
set QT_QPA_PLATFORM=offscreen
set QT_QPA_FONTDIR=C:\Windows\Fonts

cd /d "C:\DSSAT48\Tools\GB2CPP\manual_deployment"

echo Running GB2...
GB2.exe %*
echo.
echo EXIT CODE: %ERRORLEVEL%
endlocal
