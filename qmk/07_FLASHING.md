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

Four routes back:

1. **Arduino IDE / `arduino-cli` / `qmk flash` (the normal way).** The 1200-baud-touch auto-reset
   is **now implemented** in `orbit_autoreset.c` and is enabled unconditionally. The Arduino IDE
   (and `qmk flash`, which uses the same avrdude auto-detect path) opens the CDC serial port at
   1200 baud and closes it, causing the firmware to call `bootloader_jump()` and open the
   Caterina window. This is the same mechanism the Arduino core uses, so it works exactly like
   the original Arduino firmware — no manual reset required.
2. **`QK_BOOT` on button 3.** While the port is a skeleton, button 3 is mapped to `QK_BOOT`.
   (Reverts to `KC_NO` once orbit_6dof.c lands.)
3. **Physical reset.** Short `RST` to `GND` **twice, quickly** — double-tap keeps Caterina open
   ~8 s; single-tap gives only ~750 ms.
4. **Arduino IDE (returning to the Arduino sketch).** Works the same as before — no manual
   reset needed, because route 1 handles the bootloader entry.

**The bootloader is never overwritten**, so the board cannot be bricked by this. Worst case you
retry the reset timing.

### EEPROM: not erased, but the two firmwares interpret it differently

The flash command QMK uses for Caterina is `-U flash:w:...` only — **no `-U eeprom:w:`** — so
flashing does not erase EEPROM. (Verified in `platforms/avr/flash.mk`; the eeprom-writing
variants there are only used for split keyboards and DFU.)

That is *mostly* good news, but the two firmwares lay EEPROM out incompatibly:

| | Arduino v1.1.x | QMK |
|---|---|---|
| Byte 0 | magic `0xA5` | QMK's own magic |
| Byte 1 | speed mode | QMK config |
| Byte 2 | slicer mode | QMK config |

So on each transition the *other* firmware sees a wrong magic value and falls back to
defaults. In practice:

- **Arduino → QMK:** QMK detects a bad magic and initialises its own config. Fine.
- **QMK → Arduino:** the Arduino firmware finds `EEPROM_MAGIC_VALUE` missing and resets to
  `DEFAULT_SPEED_MODE` / `DEFAULT_SLICER_MOUSE_MODE`. **Your saved speed and slicer-mode
  preference are lost** — you just re-select them with the button chords.

Nothing is damaged either way; only those two remembered settings reset, and you re-select them
with the button chords.

Note that QMK's usual EEPROM-reset escape hatches are unavailable here: Bootmagic is
deliberately disabled (our chords use all three buttons, so holding one at boot must not wipe
config), and no `EE_CLR` keycode is mapped. If a corrupt EEPROM ever needs clearing, add
`EE_CLR` to a keymap temporarily, or flash with
`avrdude ... -U eeprom:w:...` explicitly.

---

## Flashing

Start the flash command first, **then** trigger the reset (if needed at all — with
`qmk flash` the auto-reset handles it automatically):

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

QMK builds, then prints:

```
Detecting USB port, reset your controller now...
```

At this point the firmware's 1200-baud-touch auto-reset should fire automatically —
`qmk flash` opens the device's CDC serial port at 1200 baud and the firmware jumps to the
bootloader. If that works you won't need to do anything manually.

If the auto-reset does not fire (another program is holding the port, or the
device enumerates without a CDC interface):

- Close Arduino IDE serial monitor, VIA, and 3DxWare — they can hold the port.
- Press button 3 (`QK_BOOT`) as a fallback.
- Or double-tap RST-to-GND.

### Manual flash, if the automatic path misbehaves

Find the bootloader's COM port (Device Manager → Ports, appears briefly as *Arduino Leonardo
bootloader*), then:

```bash
avrdude -p atmega32u4 -c avr109 -P COM7 \
        -U flash:w:hackman3d_orbit_controller_viam.hex:i
```

Adjust `COM7`. The `.hex` is written to the root of the QMK tree by the build.

### Where the `.hex` lives (and how to get it back)

Each build writes **two copies**:

| Path | Purpose |
|---|---|
| `.build/hackman3d_orbit_controller_viam.hex` | canonical build output |
| `hackman3d_orbit_controller_viam.hex` (tree root) | convenience copy for flashing |

If the root copy is missing — deleted during cleanup, say — you do **not** need to rebuild:

```bash
cp .build/hackman3d_orbit_controller_viam.hex .
```

To confirm a `.hex` is complete, check that its last line is the Intel HEX EOF record and that
the size matches the build:

```bash
tail -1 hackman3d_orbit_controller_viam.hex     # must be  :00000001FF
avr-size --target=ihex hackman3d_orbit_controller_viam.hex
```

Expect **11,500 bytes** for `default` and **12,832** for `viam` (both with VIRTSER enabled).
Note the on-disk `.hex` file is ~30–34 KB — that is Intel HEX ASCII encoding, roughly 3× the
real flash figure.

For reference, before VIRTSER was added: default 10,574 / viam 11,920. The CDC serial interface
costs 880–926 bytes and 3 USB endpoints, but we have room.

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
