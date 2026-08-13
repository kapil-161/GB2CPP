@echo off
REM Double-click this to run the GB2 headless regression smoke-test.
REM   - Compares plot metrics against saved baselines (tests\baselines\).
REM   - To (re)create baselines after an intentional change:  run_smoke_test.bat --update
cd /d "%~dp0"
python smoke_test.py %*
echo.
pause
