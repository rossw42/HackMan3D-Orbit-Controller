# Hackman3D Tuning Configurator

A browser-based GUI for editing firmware parameters in `Hackman3D_Orbit_Controller.ino` — no code editing required.

---

## Overview

`tuning.html` reads your `.ino` firmware file directly in the browser, presents every tunable parameter as a slider, toggle, or dropdown, and writes the changes back to the same file when you save. A timestamped backup is automatically downloaded before each save.

For a full explanation of what each parameter does and recommended values, see [`TUNING_GUIDE.md`](../TUNING_GUIDE.md).

---

## Requirements

- **Chrome or Edge** (desktop) — the tool uses the [File System Access API](https://developer.mozilla.org/en-US/docs/Web/API/File_System_Access_API), which is not available in Firefox or Safari.
- No installation, no server, no dependencies.

---

## How to use

1. Open `tuning.html` in Chrome or Edge (double-click the file or drag it into the browser).
2. Click **Open .ino file** and select `Hackman3D_Orbit_Controller.ino` from your Arduino sketch folder.
3. Adjust settings using the sliders, toggles, and dropdowns.
4. Click **Save to .ino** when done.
   - A `.backup_<timestamp>` file is downloaded automatically before the save.
5. Upload the firmware again with Arduino IDE.
6. Unplug and reconnect the controller — do not touch the knob during startup.

> **Tip:** Use **Discard changes** to undo all unsaved edits. Use **Reset all to defaults** to restore factory values (save afterward to apply).

---

## Parameters

Parameters are grouped into collapsible sections. Click any section header to expand or collapse it.

### Dead zones

| Parameter | Default | Range | Description |
|---|---|---|---|
| `DEADZONE_INPUT` | 40 | 30–70 | Filters electrical noise after reading the joysticks. Increase if the controller moves by itself; decrease if it needs too much force to react. |
| `DEADZONE_OUTPUT` | 45 | 25–80 | Filters small final values before sending to the computer. Increase if the camera drifts after releasing the knob; decrease if fine movements feel imprecise. |

### Smoothing & calibration

| Parameter | Default | Range | Description |
|---|---|---|---|
| `SMOOTH_DIVISOR` | 5 | 3–8 | Higher = smoother but slower. Lower = faster but less filtered. |
| `CENTER_SAMPLES` | 100 | 50–200 | Readings used during startup calibration. More = more stable center but longer startup. |

### Speed & response curve

| Parameter | Default | Range | Description |
|---|---|---|---|
| `MAX_SPEED_SCALE` | 0.70 | 0.50–1.00 | Global speed cap for all axes. |
| `RESPONSE_CURVE` | 1.60 | 1.0–2.2 | How progressively the controller reacts near center. Higher = softer start. |
| `DEFAULT_SPEED_MODE` | 1 (Normal) | 0 / 1 / 2 | Which speed mode the controller starts in (0 = Slow, 1 = Normal, 2 = Fast). |

### Speed mode profiles

Three independent profiles, each with its own scale and response curve.

| Mode | Default scale | Default curve |
|---|---|---|
| 0 — Slow | 0.50 | 1.9 |
| 1 — Normal | 0.70 (= `MAX_SPEED_SCALE`) | 1.6 (= `RESPONSE_CURVE`) |
| 2 — Fast | 1.00 | 1.3 |

Switch modes at runtime by pressing all three buttons simultaneously (configurable — see Speed mode button shortcut below).

### Translation sensitivity

| Parameter | Default | Range | Description |
|---|---|---|---|
| `GAIN_TX` | 1.3 | 1.0–3.0 | Left / right sensitivity. |
| `GAIN_TY` | 1.3 | 1.0–3.0 | Forward / backward sensitivity. |
| `GAIN_TZ` | 2.3 | 1.0–3.0 | Up / down sensitivity. |

### Rotation sensitivity

| Parameter | Default | Range | Description |
|---|---|---|---|
| `GAIN_RX` | 1.8 | 1.2–3.0 | Rotate around X. |
| `GAIN_RY` | 1.8 | 1.2–3.0 | Rotate around Y. |
| `GAIN_RZ` | 2.0 | 1.2–3.0 | Twist around Z. |

### Axis behavior

| Parameter | Default | Description |
|---|---|---|
| `ROTATION_PRIORITY` | 0.65 | Lower = rotation gets more priority over translation. Range: 0.45–1.00. |
| `ENABLE_DOMINANT_AXIS_FILTER` | Off | When on, only the strongest movement axis is sent. Useful if diagonal movements are too frequent. |

### Speed mode button shortcut

| Parameter | Default | Description |
|---|---|---|
| `MODE_SWITCH_BUTTON_COUNT` | 3 | How many buttons must be held simultaneously to cycle speed modes. Set to 0 to disable. |
| `MODE_SWITCH_SUPPRESS_BUTTONS` | On | When on, the shortcut combo is not forwarded to the computer as normal button presses. |
| `MODE_SWITCH_CHORD_WINDOW_MS` | 250 ms | Time window to complete the button combo. Increase if CAD software receives one button before the full combo; decrease if held single buttons feel delayed. Range: 150–400. |
| `MODE_SWITCH_DEBOUNCE_MS` | 500 ms | Lockout after switching speed mode. Increase if one press cycles more than one mode. Range: 300–700. |

### Slicer mouse mode

For slicers or 3D applications that do not respond to SpaceMouse HID reports. Activate at runtime by holding buttons 2 + 3.

| Parameter | Default | Description |
|---|---|---|
| `ENABLE_SLICER_MOUSE_MODE` | On | Enable or disable slicer mouse mode support entirely. |
| `DEFAULT_SLICER_MOUSE_MODE` | Off | Start in slicer mouse mode instead of CAD mode. |
| `ENABLE_SLICER_KEYBOARD_SHORTCUTS` | On | Enable keyboard shortcuts in slicer mode. |
| `SLICER_MOUSE_MOVE_DIVISOR` | 120 | Controls drag speed. Increase if view moves too fast; decrease if too slow. |
| `SLICER_MOUSE_WHEEL_THRESHOLD` | 90 | Minimum movement before zoom activates. Increase if zoom starts too easily. |
| `SLICER_MOUSE_WHEEL_MIN_INTERVAL_MS` | 45 ms | Fastest zoom repeat rate. Lower = faster zoom. |
| `SLICER_MOUSE_WHEEL_MAX_INTERVAL_MS` | 125 ms | Slowest zoom repeat rate. Higher = slower zoom. |
| `SLICER_MOUSE_AUTO_DRAG` | On | Automatically hold the mouse button while dragging in slicer mode. |
| `SLICER_BUTTON_LONG_PRESS_MS` | 650 ms | Hold duration required to trigger a long-press action in slicer mode. |

### Serial debug output

| Parameter | Default | Description |
|---|---|---|
| `DEBUG_SERIAL` | Off | Enable serial output for debugging. Keep off during normal use. |
| `DEBUG_SERIAL_INTERVAL_MS` | 100 ms | How often to print debug data. Higher = less output. |

When `DEBUG_SERIAL` is on, open the Arduino Serial Monitor or Serial Plotter at **115200 baud**.

---

## Save behaviour

- **Save to .ino** — patches all changed values directly into the `.ino` file and downloads a timestamped backup first.
- **Discard changes** — reloads all controls from the last saved state of the file.
- **Reset all to defaults** — sets every control to the factory default value. You must save afterward to write the change to the file.

---

## Recommended tuning order

Change one setting at a time. After each change, upload the firmware and test in your CAD software before adjusting the next value.

1. `DEADZONE_INPUT`
2. `DEADZONE_OUTPUT`
3. `SMOOTH_DIVISOR`
4. `MAX_SPEED_SCALE`
5. `RESPONSE_CURVE`
6. Speed mode scales and curves (if you use speed modes)
7. Slicer mouse settings (if you use slicer mouse mode)
8. `ENABLE_DOMINANT_AXIS_FILTER`
9. Individual translation and rotation gains
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

If tuning goes wrong, click **Reset all to defaults** in the configurator, then save and re-upload. The full default value table is in [`TUNING_GUIDE.md § 16`](../TUNING_GUIDE.md#16-safe-default-values).
