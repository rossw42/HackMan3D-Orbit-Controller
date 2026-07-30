# 07 — Flashing & Recovery (Caterina / Pro Micro)

## ⚠️ Read this first: what you are about to flash

The QMK port is **Phase 2 — a skeleton**. It builds, enumerates over USB, and reads the three
buttons. It does **not yet** do any of the following:

| | Status |
|---|---|
| Reads the 8 joystick channels | ❌ not wired up (Phase 3) |
| Sends 6DOF motion | ❌ not wired up (Phase 5) |
| Recognised by 3DxWare / Fusion 360 | ❌ needs the `tmk_core` patch (Phase 1, **not applied yet**) |
| Slicer mouse mode | ❌ Phase 7 |
| Speed-mode / slicer chords, LEDs | ❌ Phase 6 |
| Live keymap editing in VIA | ✅ works (`viam` build) |
| Live parameter tuning | ❌ Phase 9 |

**So the device will not function as a SpaceMouse yet.** Flashing now is only useful to confirm
USB enumeration, VID/PID, and that you can get back to the Arduino firmware. If you want a
working controller today, stay on the Arduino build.

Also note: `JOYSTICK_MULTIAXIS_ENABLE` is set in `config.h` but the matching `tmk_core` patch
does **not** exist yet, so the define currently does nothing and the device presents as a
generic 6-axis joystick — not a 3Dconnexion device.

---

## Getting back out — read before you flash

The Pro Micro's Caterina bootloader is only listening for **~750 ms after a reset**, so
recovery depends on hitting that window.

Three routes back:

1. **`QK_BOOT` on button 3.** While the port is a skeleton, physical button 3 is mapped to
   `QK_BOOT`, so pressing it jumps straight to the bootloader. This is the easy route and the
   reason it is temporarily mapped that way. (It becomes `KC_NO` once the 6DOF button report
   is wired up, since those buttons bypass the keymap.)
2. **Physical reset.** Short the `RST` pin to `GND` **twice, quickly** — a double-tap holds the
   bootloader open ~8 s instead of 750 ms. A single tap gives you only the short window.
3. **Arduino IDE.** It performs the 1200-baud-touch reset automatically, so re-flashing the
   original sketch works the same as it always did.

**The bootloader is never overwritten**, so the board cannot be bricked by this. Worst case you
retry the reset timing.

---

## Flashing

From the root of the QMK tree containing `keyboards/hackman3d/`:

```bash
# VIA build (live keymap editing)
make hackman3d/orbit_controller:viam:flash

# or the plain build
make hackman3d/orbit_controller:default:flash
```

Or with the CLI:

```bash
SKIP_GIT=true qmk flash -j 0 -kb hackman3d/orbit_controller -km viam
```

### The Caterina timing dance

QMK will build, then print:

```
Detecting USB port, reset your controller now...
```

**Only then** trigger the reset (button 3 / double-tap RST-to-GND). QMK watches for the
bootloader's serial port to appear and calls `avrdude` the moment it does.

If it says `Bootloader not found` or times out:

- You reset too early. Start the command first, reset *after* the prompt appears.
- Single-tap instead of double-tap — try again with a fast double-tap.
- Another program is holding the port (Arduino IDE serial monitor, VIA, 3DxWare). Close them.

### Manual flash, if the automatic path misbehaves

Find the bootloader's COM port (Device Manager → Ports, appears briefly as *Arduino Leonardo
bootloader*), then:

```bash
avrdude -p atmega32u4 -c avr109 -P COM7 \
        -U flash:w:hackman3d_orbit_controller_viam.hex:i
```

Adjust `COM7`. The `.hex` is written to the root of the QMK tree by the build.

---

## After flashing — what to check

1. **It enumerates.** Device Manager should show a new HID device. Until the `tmk_core` patch
   lands it will be a *generic* joystick, not "SpaceMouse Pro Wireless".
2. **VID/PID is `256F:C631`.** In Device Manager → Properties → Details → Hardware Ids.
   This confirms `keyboard.json` took effect.
3. **VIA sees it** (`viam` build only). Open VIA, enable the Design tab, load
   `qmk/via/hackman3d_orbit_controller.json` from this repo. All five tuning tabs should
   render — the sliders won't *do* anything until Phase 9, but the UI proves the Raw HID
   channel works.
4. **You can get back.** Press button 3 (`QK_BOOT`), confirm the bootloader appears, and
   re-flash the Arduino sketch. **Do this once before you rely on the board for anything.**

---

## Returning to the Arduino firmware

Nothing special required — open
`FirmwareUpdates/v1.1.0/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino` in the
Arduino IDE with the NavCore 3D Controller board package selected and upload as usual. The IDE
handles the reset itself.

You can flash back and forth freely; only the application area is rewritten.
