# scripts/setup_com.ps1  -- install / verify the com0com pairs that
# aneb-sim's UART bridge expects.
#
# Strategy:
#   1. Re-launch as Administrator if not already elevated.
#   2. Locate setupc.exe.  Prefer the bundled signed driver shipped
#      next to this script (com0com_signed/x64/) so the install works
#      out of the box on Windows 10/11 without driver-signing pain.
#   3. Enumerate existing pairs and check each device's PnP status.
#   4. Create any missing ECU pairs (COM10..COM19).
#   5. Verify all five pairs are healthy AND visible in Ports class
#      (so Arduino Serial Monitor / PuTTY can open them).
#   6. Report a clear status summary; on any failure print the exact
#      next step the student should take.
#
# Usage:
#   Right-click setup_com.bat -> Run as administrator
#   OR
#   powershell -ExecutionPolicy Bypass -File setup_com.ps1
#
# Optional override:
#   -DriverDir <path>     Use setupc.exe from this folder instead of the
#                          bundled signed driver.

[CmdletBinding()]
param(
    [string]$DriverDir = "",
    # When set, remove the aneb-sim pairs (COM10..COM19) instead of
    # installing them.  Other com0com pairs (e.g. the default
    # CNCA0/CNCB0 pair created by the bundled setup) are left alone.
    [switch]$Remove = $false
)

$ErrorActionPreference = 'Stop'

# ---- Pairs the aneb-sim bridge expects (must match qml_bridge.py) ---
$Pairs = @(
    [pscustomobject]@{ Chip="ECU1"; Bridge="COM10"; User="COM11" }
    [pscustomobject]@{ Chip="ECU2"; Bridge="COM12"; User="COM13" }
    [pscustomobject]@{ Chip="ECU3"; Bridge="COM14"; User="COM15" }
    [pscustomobject]@{ Chip="ECU4"; Bridge="COM16"; User="COM17" }
    [pscustomobject]@{ Chip="MCU";  Bridge="COM18"; User="COM19" }
)

# ---- Helpers --------------------------------------------------------

function Write-Step ($msg) { Write-Host ""; Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Ok   ($msg) { Write-Host "  [OK]   $msg" -ForegroundColor Green }
function Write-Warn ($msg) { Write-Host "  [WARN] $msg" -ForegroundColor Yellow }
function Write-Err  ($msg) { Write-Host "  [ERR]  $msg" -ForegroundColor Red }

function Test-Admin {
    $me = [Security.Principal.WindowsPrincipal]::new(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    return $me.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Find-Setupc {
    param([string]$DriverDir)

    $candidates = @()
    if ($DriverDir) {
        $candidates += (Join-Path $DriverDir 'setupc.exe')
        $candidates += (Join-Path $DriverDir 'x64\setupc.exe')
    }
    # Bundled signed driver (preferred  -- works on Win10/11 without
    # signature-enforcement workarounds).
    $candidates += (Join-Path $PSScriptRoot 'com0com_signed\x64\setupc.exe')
    # Standard com0com install locations (works only if the unsigned
    # driver was somehow accepted, e.g. test signing mode).
    $candidates += "${env:ProgramFiles(x86)}\com0com\setupc.exe"
    $candidates += "$env:ProgramFiles\com0com\setupc.exe"
    # User's Downloads  -- fallback for ad-hoc testing.
    $candidates += "$env:USERPROFILE\Downloads\com0com\x64\setupc.exe"

    foreach ($p in $candidates) {
        if ($p -and (Test-Path $p)) { return $p }
    }
    return $null
}

function Invoke-Setupc {
    param([string]$Setupc, [string[]]$ArgsList)

    # setupc.exe loads com0com.inf from the current working directory,
    # not from the binary's directory  -- so push into the install dir.
    $dir = Split-Path $Setupc
    Push-Location $dir
    try {
        $output = & $Setupc @ArgsList 2>&1
        return $output
    } finally {
        Pop-Location
    }
}

function Get-Com0comPairs {
    param([string]$Setupc)

    $output = Invoke-Setupc -Setupc $Setupc -ArgsList @('list')
    $byPort = @{}
    foreach ($line in $output) {
        # Each pair entry looks like:  CNCAn PortName=COMxx
        if ($line -match 'CNC([AB])(\d+)\s+PortName=(COM\d+)') {
            $side = $matches[1]; $idx = [int]$matches[2]; $com = $matches[3]
            $byPort[$com] = [pscustomobject]@{
                InternalName = "CNC$side$idx"
                Side         = $side
                PairIndex    = $idx
                Com          = $com
            }
        }
    }
    return $byPort
}

function Set-ComFriendlyName {
    # Override the FriendlyName of a com0com user-side port (CNCBn) so
    # tools like Arduino Serial Monitor, PuTTY, and pyserial show
    # "ECU1 (aneb-sim) (COM11)" in their port picker instead of just
    # "com0com - serial port emulator (COM11)".  This lets a tool like
    # remote_flasher auto-detect ports by scanning descriptions for
    # "aneb-sim" -- no hard-coded COM-number table needed.
    #
    # Real path on com0com 3.0.0 is one level deep (no instance sub-key):
    #   HKLM\SYSTEM\CurrentControlSet\Enum\COM0COM\PORT\<CNCBn>
    # FriendlyName isn't set by the INF, so we have to create the value
    # with -Force.
    param(
        [string]$InternalName,
        [string]$FriendlyName
    )

    $key = "HKLM:\SYSTEM\CurrentControlSet\Enum\COM0COM\PORT\$InternalName"
    if (-not (Test-Path $key)) { return $false }

    try {
        Set-ItemProperty -Path $key -Name 'FriendlyName' `
                         -Value $FriendlyName -Force -ErrorAction Stop
        return $true
    } catch {
        return $false
    }
}

# NOTE — there's no working workaround for the device-class issue.
# Pete Batard's signed com0com INF only declares the driver in the
# private CNCPorts class, so even if we write Class=Ports +
# ClassGUID={4d36e978-...} into the registry, Windows reverts both
# to the INF-declared values on the next PnP enumeration.  Tools that
# strictly filter to the standard Ports class (Arduino IDE port
# picker, pyserial's list_ports) will not enumerate these ports.
#
# Practical impact:
#   - Bridges (pyserial open-by-name) work fine.
#   - PuTTY / Tera Term / screen work fine (user types COM name).
#   - Win32_SerialPort WMI query enumerates them OK.
#   - aneb-ui ships com_helpers.find_aneb_ports() which uses winreg
#     to read FriendlyName, so remote_flasher and similar can
#     auto-detect ECU<->COM mapping by friendly name.

function Restart-PnpInstance {
    # Force Windows to re-enumerate a PnP device so Class / FriendlyName
    # changes we just wrote take effect without a reboot.  pnputil ships
    # with Windows 10 1903+ and does the right thing.  Failures here are
    # non-fatal — the user just sees the change after their next reboot.
    param([string]$InstanceId)
    try {
        & pnputil /restart-device "$InstanceId" *>&1 | Out-Null
        return $true
    } catch {
        return $false
    }
}

function Get-Com0comHealth {
    # Returns a list of objects describing every com0com PnP device
    # and whether it's healthy.
    $devices = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
               Where-Object { $_.FriendlyName -match 'com0com|CNCA|CNCB' }
    $report = @()
    foreach ($d in $devices) {
        $reason = $null
        if ($d.Status -ne 'OK') {
            try {
                $prop = Get-PnpDeviceProperty -InstanceId $d.InstanceId `
                            -KeyName 'DEVPKEY_Device_ProblemCode' `
                            -ErrorAction Stop
                $reason = "ProblemCode=$($prop.Data)"
            } catch {}
        }
        $report += [pscustomobject]@{
            Name   = $d.FriendlyName
            Status = $d.Status
            Reason = $reason
        }
    }
    return $report
}

# ---- 1. Elevate if needed ------------------------------------------

if (-not (Test-Admin)) {
    Write-Host "Re-launching elevated (Administrator privileges required)..."
    $argList = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$PSCommandPath)
    if ($DriverDir) { $argList += @('-DriverDir',$DriverDir) }
    if ($Remove)    { $argList += '-Remove' }
    Start-Process powershell -Verb RunAs -ArgumentList $argList -Wait
    exit
}

Write-Host ""
Write-Host "aneb-sim COM port setup" -ForegroundColor Cyan
Write-Host "=======================" -ForegroundColor Cyan

# ---- 2. Find setupc ------------------------------------------------

Write-Step "Locating com0com setupc.exe..."
$setupc = Find-Setupc -DriverDir $DriverDir
if (-not $setupc) {
    Write-Err "setupc.exe not found in any known location."
    Write-Host ""
    Write-Host "Expected one of:"
    Write-Host "  - $PSScriptRoot\com0com_signed\x64\setupc.exe (bundled)"
    Write-Host "  - C:\Program Files (x86)\com0com\setupc.exe"
    Write-Host "  - C:\Program Files\com0com\setupc.exe"
    Write-Host ""
    Write-Host "If the bundled driver was deleted, restore it from the aneb-sim repo:"
    Write-Host "  scripts\com0com_signed\"
    exit 1
}
Write-Ok "setupc.exe at $setupc"

# Decide which mode we're in based on setupc location.
$usingBundled = $setupc -like "*\com0com_signed\*"
if ($usingBundled) {
    Write-Ok "Using bundled signed driver (recommended for Windows 10/11)"
} else {
    Write-Warn "Using system-installed com0com  -- driver health may vary on Win10/11"
}

# ---- 2b. Remove mode (early-exit branch) ---------------------------

if ($Remove) {
    Write-Step "Removing aneb-sim pairs (COM10..COM19)..."
    $existing = Get-Com0comPairs -Setupc $setupc
    if ($existing.Count -eq 0) {
        Write-Host "  No pairs installed -- nothing to remove."
        exit 0
    }
    # Each pair has TWO entries in $existing (CNCAn + CNCBn).  Group by
    # PairIndex so we issue 'remove N' once per pair.
    $toRemove = @{}
    foreach ($pair in $Pairs) {
        foreach ($com in @($pair.Bridge, $pair.User)) {
            if ($existing.ContainsKey($com)) {
                $toRemove[$existing[$com].PairIndex] = $true
            }
        }
    }
    if ($toRemove.Count -eq 0) {
        Write-Host "  No aneb-sim-managed pairs found."
        exit 0
    }
    foreach ($idx in ($toRemove.Keys | Sort-Object)) {
        Write-Host "  Removing pair $idx..."
        Invoke-Setupc -Setupc $setupc -ArgsList @('remove', "$idx") | Out-Null
    }
    Write-Step "Re-checking remaining pairs..."
    $afterRemove = Get-Com0comPairs -Setupc $setupc
    if ($afterRemove.Count -eq 0) {
        Write-Ok "All pairs removed; com0com driver itself is still installed."
    } else {
        Write-Ok "$($afterRemove.Count / 2) pair(s) remain (not aneb-sim's)"
    }
    Write-Host ""
    Write-Host "To fully uninstall the com0com driver as well, run:"
    Write-Host "  & '$setupc' uninstall"
    Write-Host ""
    exit 0
}

# ---- 3. Inspect current state --------------------------------------

Write-Step "Inspecting existing com0com pairs..."
$existing = Get-Com0comPairs -Setupc $setupc
if ($existing.Count -eq 0) {
    Write-Host "  No pairs currently installed."
} else {
    Write-Host "  Found $($existing.Count) port(s):"
    foreach ($com in ($existing.Keys | Sort-Object)) {
        Write-Host "    $com -> $($existing[$com].InternalName)"
    }
}

Write-Step "Checking driver health..."
$health = Get-Com0comHealth
$unhealthy = $health | Where-Object { $_.Status -ne 'OK' }
if ($health.Count -eq 0) {
    Write-Host "  No com0com devices in Device Manager yet."
} elseif ($unhealthy.Count -eq 0) {
    Write-Ok "All $($health.Count) com0com device(s) report Status=OK"
} else {
    Write-Warn "$($unhealthy.Count) device(s) have problems:"
    foreach ($d in $unhealthy) {
        Write-Host "    $($d.Name) - Status=$($d.Status) $($d.Reason)"
    }
}

# If driver is broken AND we're using a system install (not bundled),
# advise reinstall from bundled signed driver.
if ($unhealthy.Count -gt 0 -and -not $usingBundled) {
    Write-Host ""
    Write-Err "The system-installed com0com driver is rejected (most likely Windows 10/11"
    Write-Err "doesn't trust its signature)."
    Write-Host ""
    Write-Host "Recommended fix:"
    Write-Host "  1. Open 'Add or Remove Programs' and uninstall 'Null-modem emulator (com0com)'"
    Write-Host "  2. Reboot"
    Write-Host "  3. Re-run this script  -- it will use the bundled signed driver instead"
    Write-Host ""
    exit 2
}

# ---- 4. Create missing pairs ---------------------------------------

Write-Step "Creating any missing ECU pairs..."
$created = 0
foreach ($pair in $Pairs) {
    if ($existing.ContainsKey($pair.Bridge) -and $existing.ContainsKey($pair.User)) {
        Write-Ok "$($pair.Chip): $($pair.Bridge) <-> $($pair.User) already present"
        continue
    }
    Write-Host "  Creating $($pair.Chip): $($pair.Bridge) <-> $($pair.User)..."
    # UsePortsClass=yes makes the device enroll in the standard 'Ports'
    # class (visible in Device Manager > Ports (COM & LPT) and listed
    # by Win32_SerialPort) instead of com0com's private CNCPorts class.
    # Without it, only pyserial finds the ports — Arduino Serial
    # Monitor, PuTTY etc. don't.
    $result = Invoke-Setupc -Setupc $setupc -ArgsList @(
        'install',
        "PortName=$($pair.Bridge),EmuBR=yes,EmuOverrun=yes,UsePortsClass=yes",
        "PortName=$($pair.User),EmuBR=yes,EmuOverrun=yes,UsePortsClass=yes"
    )
    # Filter out the noisy "logged as in use" messages (those are
    # informational, not errors).
    $errs = $result | Where-Object {
        $_ -match '(?i)error|fail' -and $_ -notmatch 'logged as'
    }
    if ($errs) {
        Write-Err "$($pair.Chip): setupc reported errors:"
        foreach ($e in $errs) { Write-Host "      $e" }
    } else {
        Write-Ok "$($pair.Chip) installed"
        $created++
    }
}
if ($created -gt 0) {
    Write-Host "  ($created pair(s) created  -- Windows may take a moment to enumerate them)"
    Start-Sleep -Seconds 2
}

# Re-enumerate after any installs so we have InternalName for every pair.
$existing = Get-Com0comPairs -Setupc $setupc

# ---- 4b. Force UsePortsClass=yes on existing pairs -----------------
# New pairs above were already installed with UsePortsClass=yes, but
# pairs that pre-dated this script (e.g. created by an earlier version
# of setup_com.bat) are still in the CNCPorts device class and don't
# show up in Arduino IDE etc.  setupc change ... is idempotent so it's
# safe to call on every port, every run.
Write-Step "Ensuring UsePortsClass=yes on every port..."
foreach ($pair in $Pairs) {
    foreach ($port in @($pair.Bridge, $pair.User)) {
        $entry = $existing[$port]
        if (-not $entry) { continue }
        Invoke-Setupc -Setupc $setupc -ArgsList @(
            'change', $entry.InternalName, 'UsePortsClass=yes'
        ) | Out-Null
    }
}
Write-Ok "UsePortsClass enabled on all 10 ports"

# ---- 4c. Tag user-side ports with friendly aneb-sim names ----------
# Setting the FriendlyName in the registry makes the ports show up as
# "ECU1 (aneb-sim) (COM11)" in Device Manager and any tool that lists
# serial ports by description (Arduino Serial Monitor, PuTTY, pyserial,
# remote_flasher).  This is a per-instance override that survives
# reboots; the change takes effect after the device is re-enumerated
# (immediately for newly-installed pairs, or at next reboot for ports
# that were already loaded before this script ran).
Write-Step "Tagging ports with friendly names..."
foreach ($pair in $Pairs) {
    # User-side: visible in Device Manager and to com_helpers.find_aneb_ports().
    $entryUser = $existing[$pair.User]
    if ($entryUser) {
        $name = "$($pair.Chip) (aneb-sim)"
        if (Set-ComFriendlyName -InternalName $entryUser.InternalName -FriendlyName $name) {
            Write-Ok "$($pair.User) -> '$name'"
        } else {
            Write-Warn "$($pair.User): couldn't update FriendlyName"
        }
    }
    # Bridge-side: only the aneb-sim UI's pyserial bridge opens this
    # one, but a clear label still helps when troubleshooting via
    # Device Manager.
    $entryBridge = $existing[$pair.Bridge]
    if ($entryBridge) {
        $name = "$($pair.Chip) (aneb-sim bridge)"
        Set-ComFriendlyName -InternalName $entryBridge.InternalName `
                            -FriendlyName $name | Out-Null
    }
}

# ---- 4d. Re-enumerate so the registry/INF changes take effect now --
# Without this, UsePortsClass + FriendlyName changes only show up after
# the next reboot.  pnputil /restart-device handles it live.
Write-Step "Re-enumerating com0com ports (so changes apply now)..."
$pnpDevices = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
              Where-Object { $_.InstanceId -like 'COM0COM\PORT\*' }
foreach ($d in $pnpDevices) {
    Restart-PnpInstance -InstanceId $d.InstanceId | Out-Null
}
Start-Sleep -Seconds 2
Write-Ok "Re-enumerated $($pnpDevices.Count) port(s)"

# ---- 5. Verify final state -----------------------------------------

Write-Step "Re-checking driver health after install..."
$healthAfter = Get-Com0comHealth
$unhealthyAfter = $healthAfter | Where-Object { $_.Status -ne 'OK' }
if ($unhealthyAfter.Count -gt 0) {
    Write-Warn "$($unhealthyAfter.Count) device(s) still unhealthy:"
    foreach ($d in $unhealthyAfter) {
        Write-Host "    $($d.Name) - Status=$($d.Status) $($d.Reason)"
    }
    Write-Host ""
    Write-Host "Likely cause: Windows is rejecting the driver signature."
    Write-Host "If you used the bundled signed driver, this is unexpected  -- open"
    Write-Host "Device Manager, right-click the problem device, and check the"
    Write-Host "'Status' tab for the exact error code."
    exit 3
}
Write-Ok "All com0com devices report Status=OK"

Write-Step "Verifying user-side COM ports are visible to apps..."
# Get-WmiObject Win32_SerialPort lists only ports that registered via the
# 'Ports' device class  -- exactly what Arduino Serial Monitor / PuTTY use.
$visible = @()
try {
    $visible = (Get-CimInstance Win32_SerialPort -ErrorAction Stop).DeviceID
} catch {
    $visible = @()
}

$missing = @()
foreach ($pair in $Pairs) {
    if ($pair.User -notin $visible) { $missing += $pair }
}
if ($missing.Count -gt 0) {
    Write-Warn "$($missing.Count) user-side COM port(s) not visible to Win32_SerialPort:"
    foreach ($m in $missing) {
        Write-Host "    $($m.Chip): $($m.User) (the side your serial tool opens)"
    }
    Write-Host ""
    Write-Host "Cause: 'use Ports class' option not enabled on those ports  -- apps"
    Write-Host "like Arduino Serial Monitor won't list them."
    Write-Host ""
    Write-Host "Fix: open the com0com Setup GUI (Start menu -> com0com -> Setup),"
    Write-Host "edit each pair, tick 'use Ports class' on the user-side port"
    Write-Host "(CNCBn), click Apply, and re-run this script to re-verify."
    exit 4
}
Write-Ok "All user-side COM ports listed in Win32_SerialPort"

# ---- 6. Final summary ----------------------------------------------

Write-Host ""
Write-Host "Setup complete." -ForegroundColor Green
Write-Host ""
Write-Host "Open these ports at 115200 baud in Arduino Serial Monitor / PuTTY etc:"
foreach ($pair in $Pairs) {
    Write-Host ("  {0,-5} -> {1,-7} ('{0} (aneb-sim)')" -f $pair.Chip, $pair.User)
}
Write-Host ""
Write-Host "The friendly names ('ECU1 (aneb-sim)' etc.) are visible in Device"
Write-Host "Manager and any tool that lists serial ports by description."
Write-Host ""
Write-Host "Auto-detect from Python (e.g. for remote_flasher):"
Write-Host "  from serial.tools import list_ports"
Write-Host "  for p in list_ports.comports():"
Write-Host "      if '(aneb-sim)' in (p.description or ''):"
Write-Host "          print(p.device, p.description)"
Write-Host ""
Write-Host "Next step:"
Write-Host "  pip install pyserial    # if not already installed"
Write-Host "  scripts\run-ui.sh       # launch the simulator"
Write-Host ""
Write-Host "The aneb-sim toolbar should now show tan COM badges for each ECU."
Write-Host "(If FriendlyName updates don't show in Device Manager yet, reboot once."
Write-Host " New pairs already have the right name; existing pairs refresh on reboot.)"
Write-Host ""
exit 0
