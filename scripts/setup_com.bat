@echo off
REM scripts\setup_com.bat — set up virtual COM port pairs for aneb-sim.
REM
REM Run ONCE from an Administrator command prompt.  Creates the five
REM com0com pairs that aneb-sim auto-bridges on launch:
REM
REM     Chip   bridge owns   user opens
REM     ----   -----------   ----------
REM     ECU1   COM10         COM11
REM     ECU2   COM12         COM13
REM     ECU3   COM14         COM15
REM     ECU4   COM16         COM17
REM     MCU    COM18         COM19
REM
REM After running, launch aneb-sim normally.  The bridge auto-attaches
REM the bridge-side ports; open the user-side port (COM11, COM13, ...)
REM in Arduino Serial Monitor / PuTTY / Tera Term at 115200 baud.

setlocal

REM Optional first arg: path to a folder containing setupc.exe.  Useful for
REM Pete Batard's signed driver bundle (extracted to e.g. Downloads\com0com\x64)
REM since that one's drivers actually load on modern Windows 10/11.
set "COM0COM_DIR=%~1"
if not "%COM0COM_DIR%"=="" goto :have_dir

if exist "%ProgramFiles(x86)%\com0com\setupc.exe" set "COM0COM_DIR=%ProgramFiles(x86)%\com0com"
if exist "%ProgramFiles%\com0com\setupc.exe"      set "COM0COM_DIR=%ProgramFiles%\com0com"

:have_dir
if "%COM0COM_DIR%"=="" (
    echo.
    echo ERROR: com0com is not installed and no path was supplied.
    echo.
    echo Either install from https://sourceforge.net/projects/com0com/
    echo (check the "use Ports class" checkbox), or pass the path to a
    echo signed driver bundle, e.g.
    echo.
    echo     scripts\setup_com.bat "C:\Users\you\Downloads\com0com\x64"
    echo.
    pause
    exit /b 1
)

if not exist "%COM0COM_DIR%\setupc.exe" (
    echo.
    echo ERROR: setupc.exe not found in %COM0COM_DIR%
    echo.
    pause
    exit /b 1
)

echo Using setupc in: %COM0COM_DIR%
echo.
echo Creating COM-port pairs (this needs Administrator privileges)...
echo.

REM setupc.exe loads com0com.inf from the CURRENT WORKING DIRECTORY,
REM not from its own folder.  pushd into the install dir before running
REM each install so it can find the .inf file.
pushd "%COM0COM_DIR%"

call setupc.exe install PortName=COM10 PortName=COM11
call setupc.exe install PortName=COM12 PortName=COM13
call setupc.exe install PortName=COM14 PortName=COM15
call setupc.exe install PortName=COM16 PortName=COM17
call setupc.exe install PortName=COM18 PortName=COM19

popd

echo.
echo Done.  Open the user-side port at 115200 baud:
echo     ECU1: COM11    ECU2: COM13    ECU3: COM15    ECU4: COM17    MCU: COM19
echo.
pause
endlocal
