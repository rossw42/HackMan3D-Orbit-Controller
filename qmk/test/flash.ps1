# flash.ps1 — flash the debug (or any) keymap to the Orbit Controller
#
# Usage:
#   .\flash.ps1                    # flashes debug keymap (default)
#   .\flash.ps1 -Keymap viam       # flashes viam keymap
#   .\flash.ps1 -Keymap default    # flashes default keymap
#   .\flash.ps1 -HexFile "C:\path\to\custom.hex"  # flash a specific hex
#
# How it works:
#   1. Snapshots the current COM ports.
#   2. Triggers the 1200-baud touch on the current device (if it has one,
#      i.e. Phase 2 / Phase 3 builds).  Skip this step if the board is
#      already in the bootloader.
#   3. Watches for a new COM port to appear (the Caterina bootloader port).
#   4. Calls avrdude to flash.
#
# Note: in Phase 1+ builds VIRTSER is disabled, so the 1200-baud touch
#       does NOT work.  Enter the bootloader manually with double-tap
#       RST->GND, then run this script — it will detect the new port.

param(
    [string]$Keymap   = "debug",
    [string]$HexFile  = "",
    [string]$Port     = "",        # force a specific port, skips detection
    [int]   $TimeoutS = 15         # seconds to wait for the bootloader port
)

$QMK_ROOT = "D:\GitHub2\qmk_firmware"
$KB        = "hackman3d/orbit_controller"

# ---- Resolve the hex file ------------------------------------------------
if (-not $HexFile) {
    $safe = $Keymap -replace '[/\\]','_'
    $name = "hackman3d_orbit_controller_$safe.hex"
    $HexFile = Join-Path $QMK_ROOT $name
}

if (-not (Test-Path $HexFile)) {
    Write-Host "ERROR: hex not found: $HexFile"
    Write-Host "Build it first:  SKIP_GIT=true qmk compile -j 0 -kb $KB -km $Keymap"
    exit 1
}

Write-Host "Hex:  $HexFile"
Write-Host "Size: $([math]::Round((Get-Item $HexFile).Length / 1KB, 1)) KB (ASCII Intel HEX)"

# ---- If a port was NOT forced, detect the bootloader port ---------------
if (-not $Port) {
    # Snapshot current COM ports
    function Get-ComPorts {
        (Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue).DeviceID
    }

    $before = @(Get-ComPorts)
    Write-Host "Current COM ports: $($before -join ', ')"

    # Try 1200-baud touch (only works when VIRTSER is enabled)
    $qmk_device = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
                  Where-Object { $_.Caption -match "SpaceMouse|HackMan|Orbit|Arduino Leonardo" } |
                  Select-Object -First 1

    if ($qmk_device) {
        Write-Host "Found QMK device on $($qmk_device.DeviceID) — attempting 1200-baud touch..."
        try {
            $sp = New-Object System.IO.Ports.SerialPort $qmk_device.DeviceID, 1200, None, 8, one
            $sp.Open()
            Start-Sleep -Milliseconds 100
            $sp.Close()
            Write-Host "1200-baud touch sent."
        } catch {
            Write-Host "1200-baud touch failed (VIRTSER likely disabled in this build)."
            Write-Host "Enter the bootloader manually: double-tap RST->GND now..."
        }
    } else {
        Write-Host "No QMK serial device found — enter the bootloader manually: double-tap RST->GND now..."
    }

    # Wait for the new bootloader COM port to appear
    Write-Host "Watching for a new bootloader COM port (timeout: ${TimeoutS}s)..."
    $deadline = (Get-Date).AddSeconds($TimeoutS)

    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 200
        $after = @(Get-ComPorts)
        $new = $after | Where-Object { $_ -notin $before }
        if ($new) {
            $Port = ($new | Select-Object -First 1)
            Write-Host "Bootloader port appeared: $Port"
            break
        }
    }

    if (-not $Port) {
        Write-Host ""
        Write-Host "ERROR: No new COM port appeared within ${TimeoutS} seconds."
        Write-Host ""
        Write-Host "Troubleshooting:"
        Write-Host "  1. Close anything holding the port (Arduino IDE monitor, VIA, 3DxWare)"
        Write-Host "  2. Double-tap RST->GND quickly and re-run this script"
        Write-Host "  3. Or flash manually:"
        Write-Host "       avrdude -p atmega32u4 -c avr109 -P COM7 -U flash:w:`"$HexFile`":i"
        exit 1
    }
}

# ---- Flash ---------------------------------------------------------------
Write-Host ""
Write-Host "Flashing to $Port ..."
$avrdude = "$env:ProgramFiles\Arduino\hardware\tools\avr\bin\avrdude.exe"
if (-not (Test-Path $avrdude)) {
    # Try QMK MSYS toolchain
    $avrdude = "S:\QMK_MSYS\opt\qmk\bin\avrdude.exe"
}
if (-not (Test-Path $avrdude)) {
    # Fall back to PATH
    $avrdude = "avrdude"
}

$conf = ""
if ($avrdude -ne "avrdude") {
    $conf_candidate = Join-Path (Split-Path $avrdude) "..\etc\avrdude.conf"
    if (Test-Path $conf_candidate) {
        $conf = "-C `"$conf_candidate`""
    }
}

$cmd = "$avrdude $conf -p atmega32u4 -c avr109 -P $Port -U flash:w:`"$HexFile`":i"
Write-Host $cmd
Invoke-Expression $cmd
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host ""
    Write-Host "Flash complete! The device will reboot automatically."
} else {
    Write-Host ""
    Write-Host "avrdude exited with code $exitCode."
    Write-Host "If the port was taken by another process, try closing it and re-run."
}
exit $exitCode
