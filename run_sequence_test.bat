@echo off
setlocal
REM Setup environment for headless Qt rendering
set PATH=C:\Qt\Tools\mingw1310_64\bin;C:\usr\bin;%PATH%
set QTFRAMEWORK_BYPASS_LICENSE_CHECK=1
set QT_QPA_PLATFORM=offscreen
set QT_QPA_FONTDIR=C:\Windows\Fonts

cd /d "C:\DSSAT48\Tools\GB2CPP\manual_deployment"

echo Running GB2 Sequence Test...
GB2.exe "C:/DSSAT48" "C:/DSSAT48/Sequence" "UFQU0002.OPG" --xvar DAS --yvar GWAD --save "C:/DSSAT48/Tools/GB2CPP/plot.png" --metrics "C:/DSSAT48/Tools/GB2CPP/metrics.csv"

echo.
echo EXIT CODE: %ERRORLEVEL%
endlocal
