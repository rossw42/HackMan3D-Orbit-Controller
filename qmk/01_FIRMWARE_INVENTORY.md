# 01 — Firmware Inventory (Source of Truth)

Complete inventory of `FirmwareUpdates/v1.1.0/Hackman3D_Orbit_Controller/`, the code being
ported. **Nothing in this document may be lost in the port.**

Files (5):

| File | Lines | Role |
|---|---|---|
| `Hackman3D_Orbit_Controller.ino` | 727 | Settings, globals, LED/EEPROM, axis pipeline, slicer mode, `setup()`/`loop()` |
| `orbit_logic.h` | 289 | Pure fixed-point math, curve LUTs, gain/speed constants |
| `orbit_buttons.h` | 249 | Debounce, chord detection, chord suppression state machine |
| `orbit_hid_descriptors.h` | 142 | 3 PROGMEM HID report descriptors |
| `orbit_slicer_hid.h` | 128 | `SlicerMouseHID_` PluggableUSB device |

---

## ⚠️ Two traps that will silently break the port

### Trap 1 — the float gain constants are dead code

`.ino` lines 58–66 declare `GAIN_TX…GAIN_RZ`, `MAX_SPEED_SCALE`, `RESPONSE_CURVE` as
`const float`. **These are documentation only.** The runtime math uses the fixed-point
values in `orbit_logic.h` (`GAIN_TX_FP = 333`, `SPEED_SCALE_FP[]`, the three curve LUTs).
Editing the floats changes nothing on-device.

The **only float used at runtime** is `ROTATION_PRIORITY` (0.65), and even that is
immediately converted to fixed point inline at `.ino:659`:

```c
rPow > (int32_t)(tPow * ROTATION_PRIORITY * 256) >> 8
```

**Port implication:** the QMK port must expose the **fixed-point** values as the editable
parameters, and must keep float out of the hot loop entirely.

### Trap 2 — `sendCommand()` arguments are Y/Z swapped

`.ino:718`:

```c
sendCommand(oRX, oRZ, oRY,   oTX, oTZ, oTY);
//          rx,  ry,  rz,    x,   y,   z     <-- parameter names
```

So the wire order is: translation = (TX, **TZ**, **TY**), rotation = (RX, **RZ**, **RY**).
The signature is `sendCommand(rx, ry, rz, x, y, z)` but it is called with Y and Z
transposed for both triples. This is the 3Dconnexion coordinate convention (Y-up vs Z-up).
**This swap is load-bearing — reproduce it exactly.**

Also note the argument *order* of the signature itself: rotation params come first, but
Report 1 (translation) is sent first.

---

## 1. Tunable parameters

Legend: **L** = live-editable target for VIA, **C** = compile-time only.

### 1.1 Deadzone / calibration

| Name | Value | Type | Meaning | Range | |
|---|---|---|---|---|---|
| `DEADZONE_INPUT` | 40 | int | Per-sensor deadband applied to `raw - center`, before axis math. Also the threshold base for Z-consensus detection. | 0–200 | **L** |
| `DEADZONE_OUTPUT` | 45 | int | Output deadband; applied 3× (in `applyResponseCurve`, after curve, after smoothing). Also the floor of the curve output range. | 0–200 | **L** |
| `CENTER_SAMPLES` | 100 | int | Startup calibration samples per channel (5 ms apart → ~500 ms). | 10–255 | **L** |
| `CALIBRATION_MIN` | 300 | int | Calibration sanity floor; center outside → error flash. | 0–1023 | **L** |
| `CALIBRATION_MAX` | 750 | int | Calibration sanity ceiling. | 0–1023 | **L** |
| `SMOOTH_DIVISOR` | 5 | int | Exponential smoothing divisor. Higher = smoother/slower. **Must be ≥ 1** (division). | 1–32 | **L** |

### 1.2 Gains — fixed point ×256 (`orbit_logic.h:266-271`)

| Name | FP value | Float equiv | Axis | Range | |
|---|---|---|---|---|---|
| `GAIN_TX_FP` | 333 | 1.3 | Translation X | 26–2560 (0.1–10.0) | **L** |
| `GAIN_TY_FP` | 333 | 1.3 | Translation Y | 26–2560 | **L** |
| `GAIN_TZ_FP` | 589 | 2.3 | Translation Z | 26–2560 | **L** |
| `GAIN_RX_FP` | 461 | 1.8 | Rotation X | 26–2560 | **L** |
| `GAIN_RY_FP` | 461 | 1.8 | Rotation Y | 26–2560 | **L** |
| `GAIN_RZ_FP` | 512 | 2.0 | Rotation Z | 26–2560 | **L** |

`applyGain(v, gainFP) = (int32_t)v * gainFP >> 8`.

### 1.3 Speed modes

| Name | Value | Meaning | |
|---|---|---|---|
| `SPEED_MODE_COUNT` | 3 | Number of modes cycled by the 3-button chord. | **C** |
| `DEFAULT_SPEED_MODE` | 1 | Boot default (overridden by EEPROM). | **L** |
| `SPEED_SCALE_FP[3]` | `{128, 179, 256}` | 0.50 / 0.70 / 1.00 × 256 — max-output scale per mode. | **L** |
| `SPEED_MODE_CURVE_IDX[3]` | `{0, 1, 2}` | Curve LUT index per mode. | **L** |

### 1.4 Response curve LUTs (`orbit_logic.h:51-84`)

Three 64-entry `uint8_t` tables, each `round(pow(i/63, curve) * 256)` — note entries
saturate at **256**, which does not fit `uint8_t`… and indeed `256` wraps to `0` in a
`uint8_t` array. **This is a latent bug in v1.1.0**: the tail entries written as `256`
actually store `0`.

- `CURVE_TABLE_1_9` (idx 0, slow/precision) — tail: `246,254,256,256,256,256,256,256`
- `CURVE_TABLE_1_6` (idx 1, default) — tail: `247,254,256,…`
- `CURVE_TABLE_1_3` (idx 2, fast) — tail: `253,256,…`

`lookupCurve()` reads `table[idx]` and `table[idx+1]`, and hardcodes `hi = 256` only when
`idx == 63`. For `idx` in 56..62 the stored `0`s produce a **collapse of the curve to near
zero at high deflection** on the affected tables.

**Port action:** store the tables as `uint16_t` (or clamp at 255 and treat 255 as full
scale). Documented in `06_TASKLIST.md` as a bug to fix during the port, not to faithfully
reproduce. Verify against hardware before/after.

### 1.5 Input-max normalisation (`orbit_logic.h:284-289`)

| Name | Expression | Value |
|---|---|---|
| `INPUT_MAX_TX_FP256` | `1024 * 333` | 340,992 |
| `INPUT_MAX_TY_FP256` | `1024 * 333` | 340,992 |
| `INPUT_MAX_TZ_FP256` | `2048 * 589` | 1,206,272 |
| `INPUT_MAX_RX_FP256` | `1024 * 461` | 472,064 |
| `INPUT_MAX_RY_FP256` | `1024 * 461` | 472,064 |
| `INPUT_MAX_RZ_FP256` | `1024 * 512` | 524,288 |

`= analogRange × gainFP`, where analogRange is 1024 for X/Y and 2048 for Z (Z sums four
channels). `applyResponseCurve` uses `inputMax = inputMaxFP256 >> 8`. These are **derived**
from the gains — when a gain becomes live-editable, its `INPUT_MAX` must be recomputed, not
stored independently.

### 1.6 Rotation priority / Z detection

| Name | Value | Meaning | |
|---|---|---|---|
| `ROTATION_PRIORITY` | 0.65 | If rotation power dominates, translation is zeroed **and its smoothing state reset**. | **L** (as FP, 166/256) |
| `ROTATION_PRIORITY_THRESHOLD` | 80 | Minimum `rPow` before the rule can fire. | **L** |
| `Z_PUSHPULL_THRESHOLD_MULT` | 2 | `abs(zPushPull) > DEADZONE_INPUT × 2` required. | **L** |
| `Z_ROTATION_THRESHOLD_MULT` | 3 | `abs(zTwist) > DEADZONE_INPUT × 3` required. | **L** |
| `Z_ROTATION_DIVISOR` | 2 | `rotZ = zTwist / 2`. | **L** |
| `ENABLE_DOMINANT_AXIS_FILTER` | false | Keep only strongest of 6 axes. | **L** (toggle) |

### 1.7 Slicer mouse

| Name | Value | Meaning | |
|---|---|---|---|
| `ENABLE_SLICER_MOUSE_MODE` | true | Master enable; gates the whole feature **and the USB interface**. | **C** |
| `DEFAULT_SLICER_MOUSE_MODE` | false | Boot default (overridden by EEPROM). | **L** |
| `ENABLE_SLICER_KEYBOARD_SHORTCUTS` | true | Master enable for button shortcuts. | **C** |
| `SLICER_MOUSE_MOVE_DIVISOR` | 120 | 6DOF value → mouse delta divisor. | **L** |
| `SLICER_MOUSE_WHEEL_THRESHOLD` | 90 | `abs(tz)` needed to enter zoom/wheel mode. | **L** |
| `SLICER_MOUSE_WHEEL_FULL_SCALE` | 700 | `abs(tz)` at which wheel repeat is fastest. | **L** |
| `SLICER_MOUSE_MAX_MOVE` | 12 | Clamp on mouse x/y per report. | **L** |
| `SLICER_MOUSE_MAX_WHEEL` | 1 | Wheel magnitude (sign inverted, see below). | **L** |
| `SLICER_MOUSE_WHEEL_MIN_INTERVAL_MS` | 45 | Fastest wheel repeat period. | **L** |
| `SLICER_MOUSE_WHEEL_MAX_INTERVAL_MS` | 125 | Slowest wheel repeat period. | **L** |
| `SLICER_MOUSE_AUTO_DRAG` | true | Auto-hold drag button while moving. | **L** (toggle) |
| `SLICER_MOUSE_BUTTON_LEFT/RIGHT/MIDDLE` | 0x01/0x02/0x04 | Mouse button bitmasks. | **C** |
| `SLICER_MOUSE_DRAG_BUTTON` | `=LEFT` | Which button auto-drag holds. | **L** (dropdown) |

### 1.8 Slicer keyboard shortcuts

12 constants — per physical button (1–3) × {short, long} × {modifier, key}. HID usage codes,
**not** Arduino keycodes.

| Button | Short | Long |
|---|---|---|
| 1 | mod `0x00`, key `0x2B` (Tab) | mod `0x06` (Shift+Alt), key `0x0A` (G) |
| 2 | mod `0x00`, key `0x11` (N) | mod `0x00`, key `0x0F` (L) |
| 3 | mod `0x01` (Ctrl), key `0x27` (0) | mod `0x00`, key `0x04` (A) |

Assembled into 4 lookup arrays at `.ino:428-447`. Naming is deliberately generic
(button number, not slicer command) for remapper compatibility.

| Name | Value | | |
|---|---|---|---|
| `SLICER_BUTTON_LONG_PRESS_MS` | 650 | Long-press threshold. | **L** |

**Port note:** in QMK these 6 actions become **keymap entries** (layer 1 = slicer layer,
short press) and a second set for long press — this is exactly what "live keymap editing"
buys us. The 12 constants are replaced by the dynamic keymap.

### 1.9 Timing / debounce / chords

| Name | Value | Meaning | |
|---|---|---|---|
| `BUTTON_DEBOUNCE_MS` | 10 | Stability window for the raw mask. | **L** |
| `MODE_SWITCH_BUTTONS` | `{0,1,2}` | Speed-mode chord = all 3 buttons. | **C** |
| `MODE_SWITCH_BUTTON_COUNT` | 3 | | **C** |
| `MODE_SWITCH_SUPPRESS_BUTTONS` | true | Suppress member buttons during chord detection. | **L** (toggle) |
| `MODE_SWITCH_CHORD_WINDOW_MS` | 250 | Grace period before suppressed buttons are forwarded. | **L** |
| `MODE_SWITCH_DEBOUNCE_MS` | 500 | Re-trigger lockout for speed cycling. | **L** |
| `SLICER_MODE_BUTTONS` | `{1,2,0}` | Slicer chord = buttons 1&2 (**only first 2 entries used**). | **C** |
| `SLICER_MODE_BUTTON_COUNT` | 2 | ⚠ Only `{1,2}` is the chord; the `0` is ignored. | **C** |
| `SLICER_MODE_HOLD_MS` | 250 | Hold duration to toggle slicer mode. | **L** |
| `SLICER_MODE_DEBOUNCE_MS` | 500 | Re-trigger lockout for slicer toggle. | **L** |

### 1.10 Pins

| Name | Value | Notes |
|---|---|---|
| `pins[8]` | `{A1, A0, A3, A2, A7, A6, A9, A8}` | Sensor order matters — see §3. QMK: `{F6, F7, F4, F5, D7, D4, B5, B4}` |
| `buttonPins[3]` | `{2, 3, 7}` | `INPUT_PULLUP`, active LOW. QMK: `D1, D0, E6` |
| `LED_TX_PIN` | 30 | Pro Micro TX LED, active LOW. QMK: `D5` |
| `LED_RX_PIN` | 17 | Pro Micro RX LED, active LOW. QMK: `B0` |

### 1.11 EEPROM

| Name | Value | |
|---|---|---|
| `EEPROM_MAGIC_ADDR` | 0 | |
| `EEPROM_MAGIC_VALUE` | 0xA5 | |
| `EEPROM_ADDR_SPEED_MODE` | 1 | |
| `EEPROM_ADDR_SLICER_MODE` | 2 | |

Persists **only** speed mode + slicer mode. QMK port replaces this with an `eeconfig`
kb datablock holding the full tuning struct.

### 1.12 LED feedback

| Name | Value | Meaning |
|---|---|---|
| `LED_BLINK_ON_MS` | 80 | Blink on time (non-blocking). |
| `LED_BLINK_OFF_MS` | 120 | Blink off time. |
| `RX_LED_HOLD_MS` | 500 | RX LED hold when slicer mode enabled. |

Behaviors: `ledSignalSpeedMode(m)` → blink `m+1` times on TX LED. `ledSignalSlicerMode(en)`
→ RX LED on for 500 ms then off / off immediately. `ledFlashCalibrationError()` → **blocking**
6× 60/60 ms flash on TX LED.

### 1.13 Axis inversion (runtime `bool`, not `const`)

| Name | Default |
|---|---|
| `invX`, `invY`, `invZ` | false, false, false |
| `invRX`, `invRY`, `invRZ` | **true, true, true** |

All 6 → **L** (toggles).

### 1.14 Debug

| Name | Value | |
|---|---|---|
| `DEBUG_SERIAL` | false | → QMK `CONSOLE_ENABLE` (debug builds only — costs flash + an endpoint) |
| `DEBUG_SERIAL_BAUD` | 115200 | n/a in QMK |
| `DEBUG_SERIAL_INTERVAL_MS` | 100 | keep as throttle |

---

## 2. HID descriptors

### 2.1 Primary — 3Dconnexion multi-axis controller (`orbit_hid_descriptors.h:25-68`)

```c
static const uint8_t hidReportDescriptor[] PROGMEM = {
  0x05, 0x01,              // Usage Page (Generic Desktop)
  0x09, 0x08,              // Usage (Multi-axis Controller)   <-- the key usage
  0xA1, 0x01,              // Collection (Application)

  // ---- Report 1: Translation ----
  0xA1, 0x00,              //   Collection (Physical)
  0x85, 0x01,              //     Report ID (1)
  0x16, 0x00, 0x80,        //     Logical Minimum (-32768)
  0x26, 0xFF, 0x7F,        //     Logical Maximum (32767)
  0x36, 0x00, 0x80,        //     Physical Minimum (-32768)
  0x46, 0xFF, 0x7F,        //     Physical Maximum (32767)
  0x09, 0x30,              //     Usage (X)
  0x09, 0x31,              //     Usage (Y)
  0x09, 0x32,              //     Usage (Z)
  0x75, 0x10,              //     Report Size (16)
  0x95, 0x03,              //     Report Count (3)
  0x81, 0x02,              //     Input (Data, Var, Abs)
  0xC0,                    //   End Collection

  // ---- Report 2: Rotation ----
  0xA1, 0x00,              //   Collection (Physical)
  0x85, 0x02,              //     Report ID (2)
  0x16, 0x00, 0x80,        //     Logical Minimum (-32768)
  0x26, 0xFF, 0x7F,        //     Logical Maximum (32767)
  0x36, 0x00, 0x80,        //     Physical Minimum (-32768)
  0x46, 0xFF, 0x7F,        //     Physical Maximum (32767)
  0x09, 0x33,              //     Usage (Rx)
  0x09, 0x34,              //     Usage (Ry)
  0x09, 0x35,              //     Usage (Rz)
  0x75, 0x10,              //     Report Size (16)
  0x95, 0x03,              //     Report Count (3)
  0x81, 0x02,              //     Input (Data, Var, Abs)
  0xC0,                    //   End Collection

  // ---- Report 3: 32 buttons ----
  0xA1, 0x00,              //   Collection (Physical)
  0x85, 0x03,              //     Report ID (3)
  0x15, 0x00,              //     Logical Minimum (0)
  0x25, 0x01,              //     Logical Maximum (1)
  0x75, 0x01,              //     Report Size (1)
  0x95, 32,                //     Report Count (32)
  0x05, 0x09,              //     Usage Page (Button)
  0x19, 1,                 //     Usage Minimum (Button 1)
  0x29, 32,                //     Usage Maximum (Button 32)
  0x81, 0x02,              //     Input (Data, Var, Abs)
  0xC0,                    //   End Collection

  0xC0                     // End Collection
};
```

Payloads: Report 1 = 6 bytes (3× int16 LE), Report 2 = 6 bytes, Report 3 = 4 bytes.

This is the descriptor QMK must be made to emit. See `03_HID_DESCRIPTOR_FORK.md`.

### 2.2 Slicer mouse (`orbit_hid_descriptors.h:78-107`)

Standard relative mouse, **Report ID 1** on the *second* interface: 3 button bits + 5 pad
bits + X/Y/Wheel as 3× int8 (−127..127).

### 2.3 Slicer keyboard (`orbit_hid_descriptors.h:117-142`)

Boot-protocol keyboard, **Report ID 2**: 8 modifier bits + 1 reserved byte + 6 keycodes
(logical max 115). Only 1 key is ever used by `sendKeyboardReport(mod, key)`.

**Port note:** both of these are replaced by QMK's native shared keyboard+mouse endpoint —
`tap_code16()` and `pointing_device`. No custom descriptor needed for the slicer side.

---

## 3. Axis math — exact pipeline

Sensor index → array position (`v[0..7]`, after `raw[i] - center[i]` and input deadzone):

```
v[0] = A1   v[1] = A0   v[2] = A3   v[3] = A2
v[4] = A7   v[5] = A6   v[6] = A9   v[7] = A8
```

Even indices (0,2,4,6) are the "push/pull" group; odd (1,3,5,7) the "twist" group.

### Order of operations (`.ino:626-708`) — do not reorder

1. `readAxes(raw)` — 8× `analogRead`
2. `v[i] = raw[i] - center[i]`
3. `applyInputDeadzone(v, 8, DEADZONE_INPUT)` — zero if `abs < 40`
4. Base axes:
   ```c
   transX = v[5] - v[1];    transY = v[7] - v[3];    transZ = 0;
   rotX   = v[4] - v[0];    rotY   = v[2] - v[6];    rotZ   = 0;
   ```
5. **Z push/pull** — `zPushPull = v[0]+v[2]+v[4]+v[6]`. If (≥3 positive OR ≥3 negative past
   `DEADZONE_INPUT`) AND `abs(zPushPull) > DEADZONE_INPUT * 2`:
   `transZ = -zPushPull; transX = 0; transY = 0;` (note the **negation**)
6. **Z twist** — `zTwist = v[1]+v[3]+v[5]+v[7]`. If (≥3 pos OR ≥3 neg) AND
   `abs(zTwist) > DEADZONE_INPUT * 3`: `rotZ = zTwist / 2; rotX = 0; rotY = 0;`
7. **Rotation priority** — if `rPow > 80 && rPow > (tPow * 0.65)`:
   zero `transX/Y/Z` **and** `smoothTX/TY/TZ` (resetting smoothing state is essential —
   otherwise translation decays instead of snapping to zero)
8. `applyGain` ×6
9. `keepOnlyDominantAxis` if enabled
10. `applyResponseCurve` ×6 with `SPEED_SCALE_FP[mode]`, `SPEED_MODE_CURVE_IDX[mode]`
11. `applyOutputDeadzone` (1st)
12. Axis inversion ×6
13. `smoothValue` ×6 into `smoothTX…smoothRZ`
14. Copy to `oTX…oRZ`, `applyOutputDeadzone` (2nd)

Note `applyResponseCurve` internally applies a deadzone too, so output deadzone is
effectively applied 3 times.

### `applyResponseCurve` algorithm

```c
if (value == 0) return 0;
mag = abs(value);
inputMax = inputMaxFP256 >> 8;
if (inputMax < deadzoneOutput + 1) inputMax = deadzoneOutput + 1;
if (mag < deadzoneOutput) return 0;
if (mag > inputMax)       mag = inputMax;
norm8 = ((mag - deadzoneOutput) * 255) / (inputMax - deadzoneOutput);   // 0..255
curved256 = lookupCurve(norm8, table);                                  // 0..256
maxOut = (inputMax * speedScaleFP) >> 8;
if (maxOut < deadzoneOutput) maxOut = deadzoneOutput;
out = deadzoneOutput + ((maxOut - deadzoneOutput) * curved256 >> 8);
return (value < 0) ? -out : out;
```

`lookupCurve`: `idx = n>>2`, `frac = (n&3)<<6`, linear-interpolate `table[idx]`→`table[idx+1]`
(`hi = 256` when `idx == 63`).

### `smoothValue`

```c
delta = target - current;
if (delta == 0) return current;
step = delta / smoothDivisor;
if (step == 0) step = (delta > 0) ? 1 : -1;   // guarantees convergence
return current + step;
```

### `keepOnlyDominantAxis`

First maximum wins on ties (strict `>` comparison), scanning `{tx,ty,tz,rx,ry,rz}`.

---

## 4. Button / chord logic

### `readDebouncedButtons`

```c
raw = bitmask of (digitalRead(pin) == LOW)
if (raw != lastRawMask) { lastRawMask = raw; lastChangeAt = now; }
else if (now - lastChangeAt >= debounceMs) { debouncedMask = raw; }
return debouncedMask;
```
Classic "stable for N ms" filter. Returns the last *confirmed* mask, so the mask lags reality
by up to 10 ms.

### Chord predicates

- `isModeSwitchComboPressed` — **subset** match: `(mask & comboMask) == comboMask`. With
  `{0,1,2}` that means all 3 pressed.
- `isSlicerModeComboPressed` — **exact** match: `mask == comboMask`, with `comboMask` built
  from the first 2 entries of `{1,2,0}` = buttons 1,2 → `0b110`.

**This asymmetry is the mutual-exclusion mechanism** (documented in the header): pressing all
3 buttons gives `0b111`, which can never equal `0b110`, so the slicer toggle cannot fire
during a speed-mode chord.

### `filterModeSwitchButtons` — the suppression state machine

Static state: `_modeSwitchChordActive`, `_modeSwitchChordComboTriggered`,
`_modeSwitchButtonsForwarded`, `_modeSwitchPendingButtons`, `_modeSwitchChordStartedAt`.

Purpose: when you press chord members one at a time, don't emit them as HID buttons
immediately — wait to see if a chord forms.

- `!suppressButtons` → passthrough, `comboAccepted = comboPressed`.
- No chord-member buttons held → if a chord was pending but never triggered and was never
  forwarded, **emit the pending buttons once on release** (so a tap of a chord member still
  registers as a button press). Then reset.
- Chord members held, first time → start the window, record pending, `startedAt = now`.
- Chord members held, subsequently → accumulate pending.
- `comboPressed` → set triggered, `comboAccepted = true`, suppress members.
- Already triggered → keep suppressing, `comboAccepted` stays true.
- Within `chordWindowMs` → keep suppressing (still waiting).
- Past the window without a chord → set `_modeSwitchButtonsForwarded = true` and forward the
  **full** mask from now on (user is just pressing buttons normally).

Edge cases to preserve: the "emit on release" path, the one-shot nature of
`_modeSwitchButtonsForwarded`, and the fact that `comboAccepted` (not `comboPressed`) drives
`updateSpeedMode`.

### `filterSlicerModeButtons`

Simple: while the slicer combo is pressed, mask out its member bits.

### Mode update logic

- `updateSpeedMode(comboAccepted)` — rising edge + `≥500 ms` since last: increment mode
  (wrap at 3), `resetSmoothing()`, `eepromSave()`, blink LED.
- `updateSlicerMouseMode(comboPressed)` — on rising edge record `startedAt`, clear
  `handled`. When held `≥250 ms` and `≥500 ms` since last switch and not yet handled:
  toggle, release mouse buttons, `resetSmoothing()`, `eepromSave()`, LED. `handled` clears on
  release. This is a **hold-to-toggle**, not tap-to-toggle.

---

## 5. Slicer mouse behavior (`sendSlicerMouse`)

```
tPow = |tx| + |ty|          rPow = |rx| + |ry| + |rz|
zoom = |tz| >= WHEEL_THRESHOLD
wheel = scaleMouseWheel(tz)

if (zoom): release drag buttons; if (wheel) send (0,0,wheel); RETURN  // zoom is exclusive
if (!AUTO_DRAG):
    if (rPow > tPow && rPow > DEADZONE_OUTPUT): x = scale(ry+rz), y = scale(rx)
    elif (tPow > DEADZONE_OUTPUT):              x = scale(tx),    y = scale(ty)
    if (x||y||wheel) send
    RETURN
// AUTO_DRAG:
if (rPow > tPow && rPow > DEADZONE_OUTPUT): hold DRAG_BUTTON; x = scale(ry+rz), y = scale(rx)
elif (tPow > DEADZONE_OUTPUT):              hold DRAG_BUTTON; x = scale(tx),    y = scale(ty)
else:                                       release drag buttons
if (x||y||wheel) send
```

Note `ry+rz` drives mouse X (combined yaw+roll), `rx` drives mouse Y.

`scaleMouseAxis(v, div, max)`: zero if `abs(v) < DEADZONE_OUTPUT`; else `v/div`, bumped to
±1 if it truncated to 0, clamped to ±max.

`scaleMouseWheel(v)`: rate-limited repeat generator.
- `mag < THRESHOLD` → reset direction, return 0
- interval = linear interpolation from `MAX_INTERVAL` (at threshold) down to `MIN_INTERVAL`
  (at `FULL_SCALE`)
- `dir = (v > 0) ? -MAX_WHEEL : +MAX_WHEEL` — **sign inverted**
- suppress if same direction and interval not elapsed; direction *change* fires immediately

### Slicer button actions (`updateSlicerMouseButtons`)

Per button: on press record time; at `≥650 ms` fire the long action once
(`slicerButtonLongHandled`); on release, if long never fired, fire the short action. Each
action first calls `releaseSlicerMouseButtons()`, then sends key down, `delay(20)`, key up.

If `suppressButtons` (either chord active): release everything and reset all per-button state.

### Mode output split (`.ino:711-720`)

```c
if (slicerMouseModeEnabled) {
  sendCommand(0,0,0,0,0,0);   // zero the 6DOF axes so CAD apps don't drift
  sendButtons(0);             // and zero the buttons
  updateSlicerMouseButtons(buttonMask, modeSwitchComboPressed || slicerModeComboPressed);
  sendSlicerMouse(oTX, oTY, oTZ, oRX, oRY, oRZ);
} else {
  releaseSlicerMouseButtons();
  sendCommand(oRX, oRZ, oRY, oTX, oTZ, oTY);   // <-- Y/Z swap, see Trap 2
  sendButtons(hidButtonMask);                   // filtered mask, not raw
}
```

Both modes send **every loop**, unconditionally — no change detection.

---

## 6. `setup()` sequence

1. Append HID descriptor node
2. `Serial.begin` if debugging
3. `pinMode(button, INPUT_PULLUP)` ×3
4. TX/RX LED outputs, both HIGH (= off)
5. `eepromLoad()`
6. **`delay(800)`** — settle time before calibration
7. `calibrateCenter()` — 100 samples × 5 ms, sanity check, error flash on failure
8. `ledSignalSpeedMode(currentSpeedMode)`

---

## 7. Behavior checklist for the port

| # | Behavior | QMK mechanism | Risk |
|---|---|---|---|
| 1 | 8× 10-bit ADC read | `analogReadPin()` | Low |
| 2 | Startup 100-sample calibration + 800 ms delay | `keyboard_post_init_kb()` | Low |
| 3 | Calibration sanity check + error flash | same | Low |
| 4 | Input deadzone | direct port | Low |
| 5 | Base axis differences (exact `v[]` indices) | direct port | **Med** — index mapping |
| 6 | Z push/pull consensus + negation | direct port | **Med** |
| 7 | Z twist consensus + `/2` | direct port | **Med** |
| 8 | Rotation priority **+ smoothing reset** | direct port | **High** — easy to forget the reset |
| 9 | Fixed-point gains | direct port | Low |
| 10 | Dominant axis filter | direct port | Low |
| 11 | Response curve LUT + `INPUT_MAX` derivation | direct port, **fix the 256→0 bug** | **High** |
| 12 | Triple output deadzone | direct port | Med |
| 13 | Axis inversion ×6 | direct port | Low |
| 14 | Smoothing with ±1 minimum step | direct port | Low |
| 15 | 6DOF reports 1 & 2 with **Y/Z swap** | forked descriptor | **High** — Trap 2 |
| 16 | 32-button report 3 | forked descriptor | Low |
| 17 | 10 ms button debounce | QMK debounce or port as-is | Low |
| 18 | Speed chord (subset, all 3) | custom keycode / `process_record_kb` | Med |
| 19 | Slicer chord (exact, 2 buttons) hold-to-toggle | custom | **High** — exact-match semantics |
| 20 | Chord suppression + emit-on-release | port `filterModeSwitchButtons` verbatim | **High** |
| 21 | Slicer relative mouse + auto-drag | `pointing_device` custom driver | Med |
| 22 | Wheel rate-limited repeat, inverted sign | direct port | Med |
| 23 | Zoom exclusivity | direct port | Low |
| 24 | Short/long press shortcuts | dynamic keymap + tap-hold | Med (improved) |
| 25 | Zeroing 6DOF while in slicer mode | direct port | Low |
| 26 | EEPROM persist speed + slicer mode | `eeconfig` kb datablock | Low |
| 27 | TX LED blink `mode+1`, non-blocking | direct port | Low |
| 28 | RX LED 500 ms hold on slicer enable | direct port | Low |
| 29 | Debug telemetry | `CONSOLE_ENABLE` (debug build) | Low |
| 30 | `resetSmoothing()` on every mode change | direct port | Med |

Highest-risk items: **8, 11, 15, 19, 20**. These get dedicated verification steps in the
task list.
