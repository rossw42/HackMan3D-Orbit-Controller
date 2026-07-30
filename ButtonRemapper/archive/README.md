# Hackman3D Orbit Controller

Firmware for an Arduino Pro Micro (ATmega32U4) that turns the board into a USB 6-axis SpaceMouse-compatible HID controller using four Hall-effect joysticks.

**Version:** 1.0.0  
**Author:** Hackman3D

---

## Hardware Requirements

- Arduino Pro Micro (ATmega32U4) or compatible board with native USB HID support
- 4× Hall-effect joysticks wired to analog pins A0–A9 (8 axes total)
- Up to 3 push buttons wired between their pins and GND

### Pin Mapping

| Joystick | Axis A | Axis B |
|----------|--------|--------|
| 1        | A1     | A0     |
| 2        | A3     | A2     |
| 3        | A7     | A6     |
| 4        | A9     | A8     |

**Button pins:** 2, 3, 7 (active LOW — connect button between pin and GND)

---

## How It Works

On startup the firmware reads 100 samples from all joystick axes to calibrate their center positions. Do not touch the controller during this calibration window (~0.5 seconds after boot).

Each main loop iteration:

1. Reads raw analog values from all 8 joystick axes
2. Subtracts the calibrated center to get relative movement
3. Applies an input deadzone to eliminate noise
4. Calculates translation (X, Y) and rotation (X, Y) from opposing sensor pairs
5. Detects Z push/pull (vertical lift) and Z rotation (twist) from consensus across all sensors
6. Applies a rotation-priority filter to prevent accidental translation during rotation
7. Applies per-axis gain, a configurable response curve, and output deadzone
8. Applies axis inversion where configured
9. Smooths all values to avoid sharp jumps
10. Sends HID reports to the host (translation report 1, rotation report 2, buttons report 3)

---

## HID Reports

The device exposes a single HID interface with three reports matching the 3Dconnexion driver format:

| Report | ID | Content              |
|--------|----|----------------------|
| Translation | 1 | X, Y, Z (int16, little-endian) |
| Rotation    | 2 | RX, RY, RZ (int16, little-endian) |
| Buttons     | 3 | 32 buttons as individual bits |

In slicer mouse mode a second HID interface exposes a standard relative mouse (Report ID 1) and an optional keyboard (Report ID 2) for sending shortcuts.

---

## Configuration

All tunable constants are defined at the top of the `.ino` file. The most commonly adjusted ones are:

### Sensitivity

```cpp
const float GAIN_TX = 1.3;   // Translation X/Y sensitivity
const float GAIN_TZ = 2.3;   // Translation Z sensitivity
const float GAIN_RX = 1.8;   // Rotation X/Y sensitivity
const float GAIN_RZ = 2.0;   // Rotation Z sensitivity
```

### Deadzones

```cpp
const int DEADZONE_INPUT  = 40;  // Applied after reading joysticks
const int DEADZONE_OUTPUT = 45;  // Applied before sending to host
```

### Response Curve

```cpp
const float RESPONSE_CURVE = 1.6;   // 1.0 = linear; higher = slower small moves
const float MAX_SPEED_SCALE = 0.70; // Global speed ceiling (0.70 = 30% slower)
```

### Speed Profiles

Three profiles cycle with a button chord (default: all three buttons pressed together):

| Mode | Scale | Response Curve |
|------|-------|----------------|
| 0    | 0.50  | 1.9 (precision) |
| 1    | 0.70  | 1.6 (default)   |
| 2    | 1.00  | 1.3 (fast)      |

### Axis Inversion

```cpp
bool invX  = false;
bool invY  = false;
bool invZ  = false;
bool invRX = true;
bool invRY = true;
bool invRZ = true;
```

Set any flag to `true` to reverse that axis.

### Other Options

| Constant | Default | Description |
|----------|---------|-------------|
| `SMOOTH_DIVISOR` | 5 | Higher = smoother but slower response |
| `ROTATION_PRIORITY` | 0.65 | Lower = rotation suppresses translation more aggressively |
| `ENABLE_DOMINANT_AXIS_FILTER` | false | When true, only the strongest axis is sent at a time |
| `CENTER_SAMPLES` | 100 | Samples taken during startup calibration |

---

## Slicer Mouse Mode

When `ENABLE_SLICER_MOUSE_MODE` is `true`, the firmware optionally emulates a standard USB mouse for use with 3D printing slicers that lack native SpaceMouse support.

**Toggle:** Hold buttons 2 + 3 for 250 ms.

In this mode the controller maps joystick movement to:
- **Mouse drag** — left-click held while moving for pan/orbit
- **Scroll wheel** — Z-axis push/pull for zoom

Button shortcuts can also be configured per button, supporting short press and long press (≥ 650 ms) actions. The default mapping targets common slicer keyboard shortcuts (Tab, Cmd+0, N, L, Cmd+Shift+G).

To disable this feature entirely, set `ENABLE_SLICER_MOUSE_MODE = false` before flashing.

---

## Debug Output

Serial debug output is disabled by default. To enable it:

```cpp
const bool DEBUG_SERIAL = true;
```

The output is compatible with the Arduino Serial Plotter and prints axis values, button state, and current speed mode at 100 ms intervals on 115200 baud.

> **Note:** Do not enable debug serial during normal HID use — it can interfere with USB timing.

---

## Building and Flashing

1. Open `Hackman3D_Orbit_Controller.ino` in the Arduino IDE
2. Select **Board:** Arduino Pro Micro (or compatible ATmega32U4 board)
3. Select the correct **Port**
4. Click **Upload**

No additional libraries are required beyond the standard Arduino `HID.h` included with the AVR board package.
