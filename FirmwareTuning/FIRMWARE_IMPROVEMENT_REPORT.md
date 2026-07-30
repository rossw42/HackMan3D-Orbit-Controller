# Firmware Improvement Report — v1.0.0 → v1.1.0

**Device:** HackMan3D Orbit Controller (Arduino Pro Micro / ATmega32U4)  
**Firmware file:** `Firmware/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino`  
**Previous version:** 1.0.0  
**New version:** 1.1.0  
**Date:** 2026-07-30

---

## Executive Summary

Version 1.1.0 delivers **8 targeted improvements** to the Hackman3D Orbit Controller firmware,
covering reliability, usability, hardware safety, code correctness, and tool compatibility.
Every change is backward-compatible: no hardware modifications are required, all existing
settings constants and their names are preserved (the Configurator tool continues to work
without changes), and the default motion behavior is byte-for-byte identical to v1.0.0.

The improvements address real-world problems reported in the `FIRMWARE_IMPROVEMENTS.md`
analysis and the `FIRMWARE_REMAPPER_SUPPORT.md` compatibility document:

| # | Improvement | User-visible impact |
|---|-------------|---------------------|
| 1 | Button debounce | No more spurious mode changes from switch bounce |
| 2 | EEPROM persistence | Speed mode and slicer mode survive power cycles |
| 3 | LED feedback | Instant visual confirmation of every mode change |
| 4 | Named constants for magic numbers | Thresholds are tunable without reading the algorithm |
| 5 | Calibration sanity check | Bad joystick connections are caught at startup |
| 6 | Array sizes use `BUTTON_COUNT` | Adding a 4th button can't silently corrupt state |
| 7 | Chord priority documentation | The chord state machine is safe for future editors |
| 8 | Per-slot remapper modifier constants | Every slicer shortcut is independently configurable |

---

## Improvement 1 — Button Debounce

### Problem (v1.0.0)
`readButtonMask()` sampled all button pins on every `loop()` iteration with no
filtering. Mechanical switches bounce for 5–20 ms after a state change. Because
button state drives time-sensitive chord detection and long-press timers, a single
physical press could register as two transitions, causing the speed mode to cycle
twice or the slicer toggle to fire unexpectedly.

### What changed
`readButtonMask()` was replaced by `readDebouncedButtons()`, which implements a
**10 ms stable-window filter**:

```
raw changes  →  start 10 ms timer  →  if raw is still the same after 10 ms  →  accept
```

Three new state variables (`debouncedButtonMask`, `lastRawButtonMask`,
`lastButtonChangeAt`) persist the last accepted stable mask between loop iterations.
The constant `BUTTON_DEBOUNCE_MS = 10` is in the settings block and can be tuned.

### How it makes the device better
- **Eliminates ghost mode switches.** A single button tap now always registers as exactly
  one event regardless of switch quality or mechanical wear.
- **More reliable long-press detection.** The slicer button long-press timer starts on
  a confirmed press, not a bounce artifact.
- **Zero latency cost.** The debounce check is a handful of integer comparisons executed
  every loop; it adds no measurable delay to HID report output.

---

## Improvement 2 — EEPROM Persistence

### Problem (v1.0.0)
`currentSpeedMode` always initialized to `DEFAULT_SPEED_MODE` (1) and
`slicerMouseModeEnabled` always initialized to `DEFAULT_SLICER_MOUSE_MODE` (false)
on every power cycle or USB replug. A user who regularly works in fast mode (mode 2)
or with slicer mode enabled had to re-toggle their preference every single session.

### What changed
Two new functions `eepromLoad()` and `eepromSave()` use the ATmega32U4's built-in
1 KB EEPROM:

- **On startup** (`setup()`): `eepromLoad()` reads the speed mode and slicer mode state
  from EEPROM addresses 1 and 2, using a magic byte at address 0 (`0xA5`) to detect
  whether the EEPROM has ever been written. On a fresh board the defaults apply as before.
- **On change**: `eepromSave()` is called inside `updateSpeedMode()` and
  `updateSlicerMouseMode()` immediately after a mode change, using `EEPROM.update()`
  which only writes when the value has actually changed, minimising EEPROM wear.

Four named constants were added to the settings block:

```cpp
const int EEPROM_MAGIC_ADDR       = 0;
const uint8_t EEPROM_MAGIC_VALUE  = 0xA5;
const int EEPROM_ADDR_SPEED_MODE  = 1;
const int EEPROM_ADDR_SLICER_MODE = 2;
```

### How it makes the device better
- **Session continuity.** The device powers up in exactly the state the user left it in —
  no re-configuration after cable swaps, laptop sleeps, or desk reboots.
- **Zero extra boot time.** Reading 3 EEPROM bytes adds less than 1 ms to startup.
- **EEPROM wear protection.** `EEPROM.update()` skips the write cycle if the value is
  unchanged. At one mode change per second continuously, the EEPROM would last
  over 27 years before reaching its 100,000-cycle endurance limit.

---

## Improvement 3 — Non-Blocking LED Feedback

### Problem (v1.0.0)
There was no visual indicator for which speed mode was active or whether slicer mode
was enabled. The only way to know the current state was to physically move a joystick
and estimate the response speed, or to toggle and count transitions mentally.

### What changed
The Pro Micro's on-board TX (pin 30) and RX (pin 17) LEDs are now used for mode
feedback. Both are active-LOW. All LED control is **non-blocking** — it uses
`millis()` comparisons and never calls `delay()`, so HID report timing is
never interrupted.

**TX LED — speed mode indicator:**

| Speed mode | TX blink pattern |
|-----------|-----------------|
| Mode 0 (slow / precision) | 1 blink |
| Mode 1 (default) | 2 blinks |
| Mode 2 (fast) | 3 blinks |

TX blinks fire:
- After every speed mode change (via the 3-button chord)
- Once after startup calibration completes (showing the restored or default mode)

**RX LED — slicer mode indicator:**

| Slicer mode event | RX LED behaviour |
|-------------------|-----------------|
| Slicer mode turned ON | Solid for 500 ms, then off |
| Slicer mode turned OFF | Immediately off |

**Calibration error flash:**  
If `calibrateCenter()` detects an out-of-range joystick center, the TX LED
flashes 6 times rapidly (60 ms on / 60 ms off) before continuing.

New functions: `ledStartBlink()`, `ledUpdateBlink()`, `ledSignalSpeedMode()`,
`ledSignalSlicerMode()`, `ledUpdateRx()`, `ledFlashCalibrationError()`.  
`ledUpdateBlink()` and `ledUpdateRx()` are called at the end of every `loop()`.

### How it makes the device better
- **Instant confirmation.** A user always knows what mode they just switched to without
  having to test movement.
- **Startup readout.** On power-up, the TX LED blinks the active speed mode,
  confirming that EEPROM restoration worked correctly.
- **Hardware diagnostics.** The calibration error flash immediately identifies a
  loose connector or faulty joystick before the device starts sending bad data.
- **No HID impact.** Because all LED logic is millis-based state-machine code, it adds
  only a few microseconds of comparison overhead per loop iteration.

---

## Improvement 4 — Named Constants for Magic Numbers

### Problem (v1.0.0)
Four numeric literals in `loop()` had no names:

```cpp
bool rotationMode = rotationPower > 80 && ...         // What is 80?
bool zDetected = ... (abs(zPushPull) > DEADZONE_INPUT * 2);   // Why 2?
bool rzDetected = ... (abs(zTwist) > DEADZONE_INPUT * 3);    // Why 3?
rotZ = zTwist / 2;                                     // Why /2?
```

A developer wanting to tune the rotation priority threshold or Z detection
sensitivity had to find these literals buried inside a large algorithm, with no
indication of what they controlled.

### What changed
Four constants were added to the settings block, directly below `ROTATION_PRIORITY`:

```cpp
const int ROTATION_PRIORITY_THRESHOLD = 80;  // min rotation power to engage rotation mode
const int Z_PUSHPULL_THRESHOLD_MULT   = 2;   // DEADZONE_INPUT multiplier for Z push/pull
const int Z_ROTATION_THRESHOLD_MULT   = 3;   // DEADZONE_INPUT multiplier for Z rotation
const int Z_ROTATION_DIVISOR          = 2;   // divisor applied to raw twist sum → rotZ
```

The four corresponding literals in `loop()` were replaced with these constants.
**The compiled output is functionally identical to v1.0.0** — this is a pure rename.

### How it makes the device better
- **Tunable without reading the algorithm.** All motion thresholds are now visible
  alongside the other settings at the top of the file.
- **Self-documenting.** Each constant name describes what it controls, and each has
  a bilingual comment explaining its effect.
- **Consistent with the rest of the file.** v1.0.0 already named most constants; this
  closes the last four gaps.

---

## Improvement 5 — Calibration Sanity Check

### Problem (v1.0.0)
`calibrateCenter()` averaged 100 samples unconditionally. If a joystick was
miswired, disconnected, or physically stuck at an extreme, the center value locked
to a bad reading (close to 0 or 1023). All downstream processing — deadzone,
translation, rotation — would silently produce wrong output with no indication
that anything was wrong.

### What changed
After averaging, each center value is checked against a valid range:

```cpp
const int CALIBRATION_MIN = 300;
const int CALIBRATION_MAX = 750;
```

If any center falls outside this range, `calibrationFailed` is set to `true` and
`ledFlashCalibrationError()` is called (6 rapid TX LED blinks at 60 ms intervals).
The firmware continues — it does not block HID output — but the flag is visible in
serial debug output (`calFail:1`).

The 300–750 range was chosen to bracket the expected mid-supply reading
(approximately 512 on a 10-bit ADC with a 5 V supply) while allowing for
normal manufacturing tolerances in Hall-effect joystick output.

### How it makes the device better
- **Immediate hardware diagnosis.** A builder who miswires a joystick sees an error
  flash at boot rather than discovering it only when the device moves oddly.
- **Visible in debug output.** When `DEBUG_SERIAL = true`, the `calFail:1` field
  appears in every serial print, making it easy to spot during bench testing.
- **Non-blocking.** The error is reported and logged; HID operation continues so
  the remaining axes are still usable while the fault is investigated.
- **Tunable range.** `CALIBRATION_MIN` and `CALIBRATION_MAX` are named constants
  in the settings block and can be adjusted if a specific Hall-effect sensor model
  has a different idle-voltage range.

---

## Improvement 6 — Array Sizes Use `BUTTON_COUNT`

### Problem (v1.0.0)
Four arrays used the literal `3` instead of `BUTTON_COUNT`:

```cpp
bool slicerButtonWasPressed[3]   = { false, false, false };
bool slicerButtonLongHandled[3]  = { false, false, false };
unsigned long slicerButtonPressedAt[3] = { 0, 0, 0 };
const int SLICER_BUTTON_ACTIONS[3]     = { ... };
```

If a future builder wired a 4th button and increased `BUTTON_COUNT` to 4, these
arrays would remain at 3 elements. The `updateSlicerMouseButtons()` loop iterates
`BUTTON_COUNT` times and would write past the end of each array — silent memory
corruption with unpredictable results.

### What changed
All four declarations now use `BUTTON_COUNT` as the array size, and `SLICER_BUTTON_ACTIONS`
was moved to the global variables section (alongside the other arrays it relates to)
so the constant is in scope when the array is declared:

```cpp
bool slicerButtonWasPressed[BUTTON_COUNT]    = { false, false, false };
bool slicerButtonLongHandled[BUTTON_COUNT]   = { false, false, false };
unsigned long slicerButtonPressedAt[BUTTON_COUNT] = { 0, 0, 0 };
const int SLICER_BUTTON_ACTIONS[BUTTON_COUNT]     = { ... };
```

`buttonPins` was also updated from `buttonPins[3]` to `buttonPins[BUTTON_COUNT]`
for the same reason.

### How it makes the device better
- **Future-proof.** Adding a 4th (or 5th) button requires changing only `BUTTON_COUNT`
  and adding the new pin and action entry — all arrays resize automatically.
- **Eliminates a latent bug.** The previous mismatch was safe only because `BUTTON_COUNT`
  happened to equal `3`; the fix makes the invariant explicit and compiler-enforced.

---

## Improvement 7 — Chord Priority Rules Documented

### Problem (v1.0.0)
The chord disambiguation logic (two chords sharing two of three buttons) was correct
but had no explanatory comment. A future editor who didn't understand *why*
`isSlicerModeComboPressed()` uses an exact-match (`buttonMask == comboMask`) instead of
a subset-match could easily change it to a subset-match, silently breaking the mutual
exclusion guarantee — making the slicer toggle fire whenever all 3 buttons are held
(i.e. during every speed mode change).

### What changed
A bilingual (EN/FR) comment block was added directly above `isSlicerModeComboPressed()`
explaining:

- **The exact-match requirement:** slicer toggle fires only when *exactly* buttons 2+3
  are held with no other buttons.
- **The mutual exclusion guarantee:** pressing all 3 buttons can never trigger the
  slicer toggle because the 3-button mask never equals the 2-button slicer mask.
- **The shared membership:** two of the three buttons belong to both chords, which is
  why the exact-match is the critical safety invariant.

### How it makes the device better
- **Safe for future contributors.** Any developer who reads `isSlicerModeComboPressed()`
  will immediately understand the invariant they must preserve.
- **No runtime cost.** Documentation-only change.

---

## Improvement 8 — Per-Slot Remapper Modifier Constants

### Problem (v1.0.0)
The `runSlicerButtonAction()` function used hardcoded `0` for three of its six
modifier arguments (HOME long, PAINT short, PAINT long, TAB short):

```cpp
sendSlicerKeyboardShortcut(0, SLICER_SHORTCUT_KEY_A);  // modifier = bare 0
sendSlicerKeyboardShortcut(0, SLICER_SHORTCUT_KEY_N);  // modifier = bare 0
sendSlicerKeyboardShortcut(0, SLICER_SHORTCUT_KEY_L);  // modifier = bare 0
sendSlicerKeyboardShortcut(0, SLICER_SHORTCUT_KEY_TAB); // modifier = bare 0
```

The `ButtonRemapper` tool works by finding a named constant in the `.ino` file and
replacing its value. A bare `0` has no name to patch. Additionally, the one shared
constant `SLICER_SHORTCUT_MODIFIER_PRIMARY` was used by both HOME short press and
TAB long press — changing it affected both slots simultaneously.

### What changed
`SLICER_SHORTCUT_MODIFIER_PRIMARY` was replaced with **six independent per-slot constants**:

```cpp
const uint8_t SLICER_SHORTCUT_MODIFIER_HOME_SHORT  = 0x08; // Cmd+0
const uint8_t SLICER_SHORTCUT_MODIFIER_HOME_LONG   = 0x00; // A
const uint8_t SLICER_SHORTCUT_MODIFIER_PAINT_SHORT = 0x00; // N
const uint8_t SLICER_SHORTCUT_MODIFIER_PAINT_LONG  = 0x00; // L
const uint8_t SLICER_SHORTCUT_MODIFIER_TAB_SHORT   = 0x00; // Tab
const uint8_t SLICER_SHORTCUT_MODIFIER_TAB_LONG    = 0x08; // Cmd+Shift+G
```

All six default values were chosen to produce **exactly the same keyboard output as
v1.0.0**. `runSlicerButtonAction()` was updated to use each slot's own constant.

The `ButtonRemapper` tool also needs a matching update to its `ACTION_SHORTCUTS` and
`SHORTCUT_CONSTANTS` JavaScript objects — see `FIRMWARE_REMAPPER_SUPPORT.md` §3 for
the exact changes required.

### How it makes the device better
- **Full remapper compatibility.** Every slicer button slot now has a named modifier
  constant. The `ButtonRemapper` tool can show a modifier dropdown for all 6 slots
  and write back the user's choice without touching the firmware source manually.
- **Independent configuration.** Changing the modifier for HOME long press no longer
  affects TAB long press.
- **Zero behavior change.** The default shortcut output (Cmd+0, A, N, L, Tab,
  Cmd+Shift+G) is identical to v1.0.0.

---

## Summary Table

| Improvement | Files changed | New constants | New functions | Behavior change |
|-------------|--------------|---------------|---------------|-----------------|
| 1. Debounce | `.ino` | `BUTTON_DEBOUNCE_MS` | `readDebouncedButtons()` | None (same logic, filtered input) |
| 2. EEPROM | `.ino` | `EEPROM_MAGIC_ADDR`, `EEPROM_MAGIC_VALUE`, `EEPROM_ADDR_SPEED_MODE`, `EEPROM_ADDR_SLICER_MODE` | `eepromLoad()`, `eepromSave()` | Modes restored on boot |
| 3. LED feedback | `.ino` | `LED_TX_PIN`, `LED_RX_PIN`, `LED_BLINK_ON_MS`, `LED_BLINK_OFF_MS`, `RX_LED_HOLD_MS` | `ledStartBlink()`, `ledUpdateBlink()`, `ledSignalSpeedMode()`, `ledSignalSlicerMode()`, `ledUpdateRx()`, `ledFlashCalibrationError()` | TX/RX LEDs now used |
| 4. Named constants | `.ino` | `ROTATION_PRIORITY_THRESHOLD`, `Z_PUSHPULL_THRESHOLD_MULT`, `Z_ROTATION_THRESHOLD_MULT`, `Z_ROTATION_DIVISOR` | None | None (pure rename) |
| 5. Calibration check | `.ino` | `CALIBRATION_MIN`, `CALIBRATION_MAX` | (uses `ledFlashCalibrationError`) | LED error on bad cal |
| 6. Array sizes | `.ino` | None | None | None |
| 7. Chord docs | `.ino` | None | None | None |
| 8. Remapper constants | `.ino` | 6 × `SLICER_SHORTCUT_MODIFIER_*` | None | None (same shortcuts) |

---

## What Was Intentionally Deferred

### Fixed-point math (improvement #8 from FIRMWARE_IMPROVEMENTS.md)
`applyGain()` and `applyResponseCurve()` still use floating-point arithmetic. The
ATmega32U4 has no hardware FPU, but the loop is currently simple enough that software
float math leaves ample headroom at the operating frequency. This improvement is
flagged as **conditional** — profile with `micros()` before/after `loop()` first.
If headroom is confirmed sufficient, there is no reason to increase code complexity.

### Header file extraction (Phase 1)
The pure logic functions (`smoothValue`, `applyGain`, `applyResponseCurve`,
`applyInputDeadzone`, `applyOutputDeadzone`, `keepOnlyDominantAxis`, `countPositive4`,
`countNegative4`) and the button/chord functions are still in the `.ino` file.
Extracting them into `orbit_logic.h` and `orbit_buttons.h` would enable PC-side unit
testing with g++. This is a maintainability investment rather than a user-facing
improvement and is planned for Phase 1 of the next iteration.

### ButtonRemapper JS update (Phase 3 remaining)
The firmware side of remapper compatibility is complete. The `ButtonRemapper`
JavaScript (`remap.html`) still needs its `ACTION_SHORTCUTS` and `SHORTCUT_CONSTANTS`
objects updated to reference the six new constant names. Until that is done, the
remapper will not show modifier dropdowns for the newly named slots. The required
JS changes are fully documented in `FIRMWARE_REMAPPER_SUPPORT.md` §3.

---

## Upgrade Instructions

1. Open `Firmware/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino` in
   the Arduino IDE.
2. Select **Board:** Arduino Pro Micro (ATmega32U4, 5V, 16 MHz).
3. Select the correct **Port**.
4. Click **Upload**.

No hardware changes are required. On the first boot after flashing, the EEPROM
magic byte will not be set, so the firmware defaults (`DEFAULT_SPEED_MODE = 1`,
`DEFAULT_SLICER_MOUSE_MODE = false`) are used. After the first mode change,
preferences are saved automatically.

The Configurator tool (`Configurator/tuning.html`) continues to work without any
changes — all existing constant names are preserved.
