@echo off
REM scripts/setup_com.bat — student-friendly entry point for setting up
REM the COM-port pairs that aneb-sim's UART bridge needs.
REM
REM Double-click this file from Explorer (or run it from a terminal).
REM It will hand off to setup_com.ps1 which does the real work:
REM
REM   - elevates to Administrator if needed
REM   - locates setupc.exe (prefers the bundled signed driver shipped
REM     in scripts\com0com_signed\, so Windows 10/11 driver-signature
REM     enforcement doesn't reject the kernel module)
REM   - creates the five pairs ECU1..ECU4 + MCU as COM10..COM19
REM   - verifies every pair is healthy in Device Manager AND visible
REM     to apps via the Ports class
REM   - prints a precise next-step instruction if anything is wrong
REM
REM Optional: pass a custom driver bundle path, e.g.
REM   setup_com.bat "C:\Users\me\Downloads\com0com\x64"

setlocal

powershell -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0setup_com.ps1" %*

REM Keep the window open so a double-click run leaves output visible
REM but skip the pause when called programmatically (the Python UI
REM sets ANEB_SIM_NONINTERACTIVE before invoking).
if defined ANEB_SIM_NONINTERACTIVE goto :end
echo.
pause
:end
endlocal
