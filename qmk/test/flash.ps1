# Manual 1200-baud-touch test for orbit_autoreset.c
#
# orbit_autoreset.c overrides LUFA's ATTR_WEAK EVENT_CDC_Device_LineEncodingChanged()
# so that any program opening the CDC serial port at 1200 baud causes the firmware
# to call bootloader_jump() - exactly what the Arduino core does.
#
# This script manually triggers that mechanism and reports the new bootloader COM port.
#
# Usage:
#   1. Flash the QMK firmware first:  qmk flash -kb hackman3d/orbit_controller -km viam
#      (qmk flash does the 1200-baud-touch itself; use this script only to test manually)
#   2. Find the device's COM port (Device Manager -> Ports) and update COM5 below.
#   3. Run:  .\flash.ps1
#   4. A new COM port (bootloader) should appear - use that with avrdude or qmk flash.
#
# If no new port appears:
#   - Another program may be holding the port (Arduino IDE serial monitor, VIA, 3DxWare)
#   - Try button 3 (QK_BOOT) as a fallback
#   - Or double-tap RST->GND

$before = (Get-CimInstance Win32_SerialPort).DeviceID

$port = New-Object System.IO.Ports.SerialPort COM10,1200,None,8,one
$port.Open()
Start-Sleep -Milliseconds 100
$port.Close()

Start-Sleep -Seconds 2

$after = (Get-CimInstance Win32_SerialPort).DeviceID

$new = $after | Where-Object { $_ -notin $before }

if ($new) {
    Write-Host "Bootloader port: $new"
    Write-Host "Flash with:"
    Write-Host "  avrdude -p atmega32u4 -c avr109 -P $new -U flash:w:D:\GitHub2\qmk_firmware\hackman3d_orbit_controller_viam.hex:i"
} else {
    Write-Host "No new COM port found."
    Write-Host "Is another program holding COM5? Try: Get-Process | Where-Object { `$_.MainWindowTitle -like '*COM5*' }"
}
