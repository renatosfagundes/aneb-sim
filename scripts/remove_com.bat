@echo off
REM scripts/remove_com.bat -- remove the aneb-sim COM-port pairs
REM (COM10..COM19) created by setup_com.bat.
REM
REM Other com0com pairs that were created independently (e.g. the
REM default CNCA0/CNCB0 pair) are left alone.  The com0com driver
REM itself stays installed.  To completely uninstall the driver,
REM use Windows "Add or Remove Programs", or run setupc directly:
REM   & "scripts\com0com_signed\x64\setupc.exe" uninstall

setlocal

powershell -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0setup_com.ps1" -Remove %*

if defined ANEB_SIM_NONINTERACTIVE goto :end
echo.
pause
:end
endlocal
