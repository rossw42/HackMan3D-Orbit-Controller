# Firmware Improvement Suggestions

Review of `Hackman3D_Orbit_Controller.ino` — findings and proposals.

---

## 1. Settings don't persist across power cycles

**Problem:** `currentSpeedMode` and `slicerMouseModeEnabled` reset to their defaults every time the board is power-cycled or USB-replugged. If you regularly use fast mode or slicer mode, you have to re-toggle every session.

**Suggestion:** Store both values in ATmega32U4 EEPROM (1 KB available). Write only on change to avoid wear. Read at startup to restore the user's last-used state.

```cpp
#include <EEPROM.h>

#define EEPROM_ADDR_SPEED_MODE     0
#define EEPROM_ADDR_SLICER_MODE    1
#define EEPROM_MAGIC_ADDR          2
#define EEPROM_MAGIC_VALUE         0xA5  // Marks EEPROM as initialized

// In setup():
if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VALUE) {
  currentSpeedMode = constrain(EEPROM.read(EEPROM_ADDR_SPEED_MODE), 0, SPEED_MODE_COUNT - 1);
  slicerMouseModeEnabled = EEPROM.read(EEPROM_ADDR_SLICER_MODE) != 0;
}

// After a mode change:
EEPROM.update(EEPROM_ADDR_SPEED_MODE, currentSpeedMode);
EEPROM.update(EEPROM_ADDR_SLICER_MODE, slicerMouseModeEnabled ? 1 : 0);
EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
```

`EEPROM.update()` only writes when the value differs, avoiding unnecessary erase/write cycles.

---

## 2. No button debounce

**Problem:** `readButtonMask()` reads each button pin directly every loop iteration with no debounce. Mechanical switches bounce for 5–20 ms after state changes. Since button state drives time-sensitive chord detection and long-press timers, a bouncy contact can cause spurious mode switches or double-triggered shortcuts.

**Suggestion:** Add a simple "stable for N ms" filter:

```cpp
const unsigned long BUTTON_DEBOUNCE_MS = 10;

uint32_t debouncedButtonMask = 0;
uint32_t lastRawButtonMask = 0;
unsigned long lastButtonChangeAt = 0;

uint32_t readDebouncedButtons() {
  uint32_t raw = 0;
  for (int i = 0; i < BUTTON_COUNT; i++) {
    if (digitalRead(buttonPins[i]) == LOW) {
      raw |= (1UL << i);
    }
  }

  if (raw != lastRawButtonMask) {
    lastRawButtonMask = raw;
    lastButtonChangeAt = millis();
  } else if (millis() - lastButtonChangeAt >= BUTTON_DEBOUNCE_MS) {
    debouncedButtonMask = raw;
  }

  return debouncedButtonMask;
}
```

---

## 3. Overlapping chord membership

**Problem:** `MODE_SWITCH_BUTTONS` uses all 3 buttons. `SLICER_MODE_BUTTONS` uses buttons 2+3. Two of the three buttons are members of both chords, requiring a complex state machine (`modeSwitchChordActive`, `modeSwitchPendingButtons`, chord windows, a second filter pass) to disambiguate which chord the user intended. This is the hardest part of the firmware to reason about and the most fragile to modify.

**Suggestion:** If a hardware or wiring change is feasible, assign each chord at least one exclusive button. If not, document the priority rules explicitly in the code:

- Slicer toggle only fires when *exactly* buttons 2+3 are held (no other buttons)
- Speed mode only fires when *all 3* are held

This mutual exclusion is already enforced, but add a comment block at the top of the chord section making it clear, so future editors don't accidentally break the invariant.

---

## 4. No calibration sanity check

**Problem:** `calibrateCenter()` averages 100 readings with no validation. If a joystick is miswired, disconnected, or physically stuck at an extreme, the center locks to a bad value silently. Everything downstream (deadzone, translation) will be wrong with no indication.

**Suggestion:** After calibration, check that each center is in a sane range:

```cpp
const int CALIBRATION_MIN = 300;
const int CALIBRATION_MAX = 750;

for (int i = 0; i < 8; i++) {
  if (center[i] < CALIBRATION_MIN || center[i] > CALIBRATION_MAX) {
    // Flash the TX LED or set a flag for debug output
    // Optionally: block HID output until recalibrated
  }
}
```

---

## 5. No feedback for current state

**Problem:** There is no visual or audible indication of which speed mode is active or whether slicer mode is enabled. The only way to know is to move a joystick and observe the response behaviour.

**Suggestion:** Use the Pro Micro's on-board TX/RX LEDs (pins 30 and 17 on ATmega32U4) for quick status feedback:

| State | LED pattern |
|-------|-------------|
| Speed mode 0 (slow) | TX blinks 1× on change |
| Speed mode 1 (default) | TX blinks 2× on change |
| Speed mode 2 (fast) | TX blinks 3× on change |
| Slicer mode on | RX LED solid for 500 ms |
| Slicer mode off | RX LED off |

Keep it non-blocking (millis-based, not delay) so HID reports aren't interrupted.

---

## 6. Hardcoded array sizes

**Problem:** Several arrays use the literal `3` instead of `BUTTON_COUNT`:

```cpp
bool slicerButtonWasPressed[3] = { ... };
bool slicerButtonLongHandled[3] = { ... };
unsigned long slicerButtonPressedAt[3] = { ... };
const int SLICER_BUTTON_ACTIONS[3] = { ... };
```

**Suggestion:** Replace with `BUTTON_COUNT` so adding a 4th button later doesn't silently leave these undersized.

---

## 7. Magic numbers in the main loop

**Problem:** Most thresholds are named constants at the top, but a few remain as unnamed literals:

- `rotationPower > 80` — minimum rotation power before rotation mode kicks in
- `DEADZONE_INPUT * 2` — Z push/pull detection threshold
- `DEADZONE_INPUT * 3` — Z rotation detection threshold
- `zTwist / 2` — rotZ halving factor

**Suggestion:** Move these to named constants in the settings section:

```cpp
const int ROTATION_PRIORITY_THRESHOLD = 80;
const int Z_PUSHPULL_THRESHOLD_MULT   = 2;
const int Z_ROTATION_THRESHOLD_MULT   = 3;
const int Z_ROTATION_DIVISOR          = 2;
```

---

## 8. Floating-point math on an FPU-less chip

**Problem:** `applyGain()` and `applyResponseCurve()` (including `pow()`) run floating-point math on every loop iteration for all 6 axes. The ATmega32U4 has no hardware FPU — this is all software-emulated.

**Impact today:** Likely fine. The loop is simple and running at hundreds of Hz. But it's the first bottleneck you'd hit if more per-loop work is added.

**Suggestion:** If loop time ever becomes a concern, convert gains to fixed-point (multiply by 256, do integer math, shift right 8). Replace `pow(x, curve)` with a pre-computed lookup table or piecewise linear approximation. Profile before optimising — `micros()` before/after `loop()` under serial debug will show current headroom.

---

## 9. No testable separation of logic from hardware

**Problem:** All the pure math (response curve, smoothing, deadzone, chord detection) is interleaved with hardware calls (`analogRead`, `digitalRead`, `HID().SendReport`). The only way to verify these algorithms is to flash the board and physically test.

**Suggestion:** Extract pure logic into a separate `.h` file with no Arduino dependencies:

```
orbit_logic.h  — smoothValue(), applyGain(), applyResponseCurve(),
                 applyInputDeadzone(), applyOutputDeadzone(), keepOnlyDominantAxis()
```

This allows writing PC-side unit tests (plain C++ compiled with gcc) to validate edge cases, especially for the chord state machine which is the hardest to test on hardware.

---

## Priority order

If tackling these incrementally:

1. **Button debounce** — low effort, eliminates a class of real-world ghost inputs
2. **EEPROM persistence** — small change, big usability improvement
3. **LED feedback** — quality of life, helps confirm mode changes worked
4. **Named constants** — housekeeping, makes tuning easier for others
5. **Calibration check** — safety net for hardware issues
6. **Array sizes** — one-line fixes, future-proofing
7. **Logic separation** — investment in long-term maintainability
8. **Fixed-point math** — only if performance becomes a concern
9. **Chord simplification** — only feasible with hardware or UX redesign
