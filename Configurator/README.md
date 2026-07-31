# Hackman3D Orbit Controller — Configurator

A single browser-based GUI for **tuning firmware parameters** and **remapping slicer button shortcuts** in `Hackman3D_Orbit_Controller.ino` — no code editing required.

**File:** [`HackMan3D_Orbit_Controller-configurator.html`](./HackMan3D_Orbit_Controller-configurator.html)

---

## Overview

The configurator reads your `.ino` firmware file directly in the browser and presents two tabs:

- **⚙ Tuning** — every tunable parameter as a slider, toggle, or dropdown (dead zones, smoothing, sensitivity, speed modes, slicer mouse mode, LED feedback, debounce, debug output, etc).
- **⌨ Button Remapping** — short-press / long-press keyboard shortcut assignment for all three physical buttons in slicer mouse mode.

Both tabs share one **Save all changes to .ino** action, so tuning and remapping changes are written together in a single pass. A timestamped backup is automatically downloaded before each save.

This tool replaces the two previous standalone tools (`Configurator/tuning.html` and `ButtonRemapper/remap.html`), which are kept in the repository for reference but are no longer the recommended entry point.

For a full explanation of what each tuning parameter does and recommended values, see [`TUNING_GUIDE.md`](../TUNING_GUIDE.md).

---

## Requirements

- **Chrome or Edge** (desktop) — the tool uses the [File System Access API](https://developer.mozilla.org/en-US/docs/Web/API/File_System_Access_API), which is not available in Firefox or Safari.
- No installation, no server, no dependencies.

---

## How to use

1. Open `HackMan3D_Orbit_Controller-configurator.html` in Chrome or Edge (double-click the file or drag it into the browser).
2. Click **Open .ino file** and select `Hackman3D_Orbit_Controller.ino` from your Arduino sketch folder.
3. Use the **⚙ Tuning** tab to adjust controller parameters, and/or the **⌨ Button Remapping** tab to change keyboard shortcuts.
4. Click **Save all changes to .ino** when done.
   - A `.backup_<timestamp>` file is downloaded automatically before the save.
5. Upload the firmware again with Arduino IDE.
6. Unplug and reconnect the controller — do not touch the knob during startup.

> **Tip:** Each tab has its own **Discard changes** button to undo unsaved edits in that tab only. The Tuning tab also has **Reset tuning to defaults** to restore factory tuning values (save afterward to apply). Discarding/resetting one tab does not affect the other.

---

## Firmware version detection

When a file loads, a badge next to the file name shows the detected firmware version (parsed from the `Version:` comment in the `.ino` header). The tool works with both:

- **v1.0.0** — the shipped monolithic `.ino` (single file, float math).
- **v1.1.0** — the refactored `.ino` + `orbit_logic.h` + `orbit_buttons.h` + `orbit_hid_descriptors.h` + `orbit_slicer_hid.h` (fixed-point math, EEPROM persistence, LED feedback, button debounce).

### ⚠ Fixed-point firmware notice (v1.1.0+)

If the opened `.ino` includes `orbit_logic.h`, a warning banner appears at the top of the Tuning tab. On this firmware:

- `GAIN_TX/TY/TZ/RX/RY/RZ`, `MAX_SPEED_SCALE`, and `RESPONSE_CURVE` are kept in the `.ino` as **documentation-only** constants. The real math uses fixed-point equivalents (`GAIN_*_FP`, `SPEED_SCALE_FP[]`, `CURVE_TABLE_*`) defined in `orbit_logic.h`. Editing these sliders saves harmless values to the `.ino` but has **no effect on-device**.
- The **Speed mode profiles** section is display-only on this firmware — the `SPEED_MODE_SCALE[]` / `SPEED_MODE_RESPONSE_CURVE[]` arrays no longer exist in v1.1.0 (replaced by `SPEED_SCALE_FP[]` + lookup tables), so edits there are silently discarded on save.
- To change real sensitivity/speed/response-curve behavior on v1.1.0 firmware, edit `orbit_logic.h` directly.
- **All other sections work normally** on both firmware versions: dead zones, smoothing/calibration, axis behavior, Z-axis gesture detection, slicer mouse mode, button debounce, LED feedback, speed-mode/slicer-mode button shortcuts, and serial debug.

---

## Tuning tab — parameters

Parameters are grouped into collapsible sections. Click any section header to expand or collapse it. Sections/rows marked **NEW** were introduced in firmware v1.1.0 and are harmless no-ops if present in a v1.0.0 file that doesn't declare them (defaults are used).

### Dead zones

| Parameter | Default | Range | Description |
|---|---|---|---|
| `DEADZONE_INPUT` | 40 | 30–70 | Filters electrical noise after reading the joysticks. |
| `DEADZONE_OUTPUT` | 45 | 25–80 | Filters small final values before sending to the computer. |

### Smoothing & calibration

| Parameter | Default | Range | Description |
|---|---|---|---|
| `SMOOTH_DIVISOR` | 5 | 3–8 | Higher = smoother but slower. |
| `CENTER_SAMPLES` | 100 | 50–200 | Readings used during startup calibration. |
| `CALIBRATION_MIN` **NEW** | 300 | 100–500 | Lowest acceptable analog center value at startup before the error LED flashes. |
| `CALIBRATION_MAX` **NEW** | 750 | 600–950 | Highest acceptable analog center value at startup before the error LED flashes. |

### Speed & response curve

| Parameter | Default | Range | Description |
|---|---|---|---|
| `MAX_SPEED_SCALE` | 0.70 | 0.50–1.00 | Global speed cap for all axes. *(Fixed-point firmware: display-only, see warning above.)* |
| `RESPONSE_CURVE` | 1.60 | 1.0–2.2 | How progressively the controller reacts near center. *(Fixed-point firmware: display-only.)* |
| `DEFAULT_SPEED_MODE` | 1 (Normal) | 0/1/2 | Which speed mode the controller starts in. |

### Speed mode profiles

Three independent profiles (scale + curve). *(Fixed-point firmware: display-only — see warning above.)*

### Translation / Rotation sensitivity

`GAIN_TX`, `GAIN_TY`, `GAIN_TZ`, `GAIN_RX`, `GAIN_RY`, `GAIN_RZ` — see [`TUNING_GUIDE.md`](../TUNING_GUIDE.md) for full defaults/ranges. *(Fixed-point firmware: display-only.)*

### Axis behavior

| Parameter | Default | Description |
|---|---|---|
| `ROTATION_PRIORITY` | 0.65 | Lower = rotation gets more priority over translation. |
| `ROTATION_PRIORITY_THRESHOLD` **NEW** | 80 | Minimum combined rotation strength before priority can cancel translation. |
| `ENABLE_DOMINANT_AXIS_FILTER` | Off | When on, only the strongest movement axis is sent. |

### Z-axis gesture detection **NEW**

| Parameter | Default | Description |
|---|---|---|
| `Z_PUSHPULL_THRESHOLD_MULT` | 2 | Multiplier on `DEADZONE_INPUT` required to detect a push/pull gesture. |
| `Z_ROTATION_THRESHOLD_MULT` | 3 | Multiplier on `DEADZONE_INPUT` required to detect a twist gesture. |
| `Z_ROTATION_DIVISOR` | 2 | Divides the raw twist signal before it becomes RZ output. |

### Speed mode button shortcut

`MODE_SWITCH_BUTTON_COUNT`, `MODE_SWITCH_SUPPRESS_BUTTONS`, `MODE_SWITCH_CHORD_WINDOW_MS`, `MODE_SWITCH_DEBOUNCE_MS` — unchanged from v1.0.0, see [`TUNING_GUIDE.md`](../TUNING_GUIDE.md).

### Slicer mouse mode

All original v1.0.0 parameters plus two **NEW** additions:

| Parameter | Default | Description |
|---|---|---|
| `SLICER_MODE_HOLD_MS` **NEW** | 250 ms | How long the CAD/slicer toggle combo must be held before switching. |
| `SLICER_MODE_DEBOUNCE_MS` **NEW** | 500 ms | Minimum time between two CAD/slicer mode toggles. |

### Button debounce **NEW**

| Parameter | Default | Description |
|---|---|---|
| `BUTTON_DEBOUNCE_MS` | 10 ms | How long a button reading must stay stable before being accepted. Prevents mechanical switch bounce from causing spurious mode changes. |

### LED feedback **NEW**

| Parameter | Default | Description |
|---|---|---|
| `LED_BLINK_ON_MS` | 80 ms | TX LED on-time during each speed-mode blink. |
| `LED_BLINK_OFF_MS` | 120 ms | TX LED off-time between speed-mode blinks. |
| `RX_LED_HOLD_MS` | 500 ms | How long the RX LED stays solid after toggling slicer mouse mode. |

### Serial debug output

`DEBUG_SERIAL`, `DEBUG_SERIAL_INTERVAL_MS` — unchanged from v1.0.0.

---

## Button Remapping tab

Each of the 3 physical buttons has a **short-press** and **long-press** keyboard shortcut used while **slicer mouse mode** is active. Each slot shows:

- A **Modifier** dropdown (None, Ctrl, Shift, Alt, Cmd, and combinations).
- A **Key** dropdown (letters, numbers, function keys, navigation keys, Tab/Enter/Escape/etc).
- A live preview of the resulting shortcut (e.g. `⌘` `L`).

Each physical button has its own generic, independent set of four constants — no "action" concept to track:

```cpp
SLICER_SHORTCUT_MODIFIER_BUTTON1_SHORT / SLICER_SHORTCUT_KEY_BUTTON1_SHORT
SLICER_SHORTCUT_MODIFIER_BUTTON1_LONG  / SLICER_SHORTCUT_KEY_BUTTON1_LONG
SLICER_SHORTCUT_MODIFIER_BUTTON2_SHORT / SLICER_SHORTCUT_KEY_BUTTON2_SHORT
SLICER_SHORTCUT_MODIFIER_BUTTON2_LONG  / SLICER_SHORTCUT_KEY_BUTTON2_LONG
SLICER_SHORTCUT_MODIFIER_BUTTON3_SHORT / SLICER_SHORTCUT_KEY_BUTTON3_SHORT
SLICER_SHORTCUT_MODIFIER_BUTTON3_LONG  / SLICER_SHORTCUT_KEY_BUTTON3_LONG
```

Button numbering (1, 2, 3) matches the badge shown on each card in the UI. All 12 constants are
independently patchable — changing one never affects another button's shortcut. This naming
replaces the earlier action-based scheme (`SLICER_BUTTON_ACTION_HOME/PAINT/TAB_SEND` +
`SLICER_BUTTON_ACTIONS[]`), which required the tool and the user to track an extra layer of
indirection between "button" and "action" that added no real value.


---

## Save behavior

- **Save all changes to .ino** — patches all changed tuning values and button remap constants directly into the `.ino` file in a single write, and downloads a timestamped backup first.
- **Discard tuning changes** — reloads Tuning tab controls from the last saved state of the file.
- **Reset tuning to defaults** — sets every Tuning tab control to the factory default value (does not affect Button Remapping). Save afterward to write the change to the file.
- **Discard remap changes** — reloads Button Remapping tab controls from the last saved state of the file.

---

## Recommended tuning order

1. `DEADZONE_INPUT`
2. `DEADZONE_OUTPUT`
3. `SMOOTH_DIVISOR`
4. `MAX_SPEED_SCALE` *(v1.0.0 only — see fixed-point notice for v1.1.0)*
5. `RESPONSE_CURVE` *(v1.0.0 only)*
6. Speed mode scales and curves, if used *(v1.0.0 only)*
7. Slicer mouse settings, if used
8. `ENABLE_DOMINANT_AXIS_FILTER`
9. Individual translation and rotation gains *(v1.0.0 only)*
10. `ROTATION_PRIORITY`

---

## Startup calibration reminder

The controller calibrates its center position every time it is powered on.

1. Place it on a stable surface before plugging in the USB cable.
2. Do not touch the knob.
3. Wait about one second before using it.

If the knob is touched during startup, unplug and reconnect without touching the knob.

---

## Safe default values

If tuning goes wrong, click **Reset tuning to defaults** in the configurator, then save and re-upload. The full default value table is in [`TUNING_GUIDE.md § 16`](../TUNING_GUIDE.md#16-safe-default-values).

---

## Legacy standalone tools

The original single-purpose tools remain in the repository for reference and are still functional:

- [`tuning.html`](./tuning.html) — tuning-only, does not include the v1.1.0 parameters or button remapping.
- [`../ButtonRemapper/remap.html`](../ButtonRemapper/remap.html) — button remapping only, does not include tuning.

New users should use `HackMan3D_Orbit_Controller-configurator.html` instead, since it covers both tools' functionality in one place with firmware-version-aware warnings.
