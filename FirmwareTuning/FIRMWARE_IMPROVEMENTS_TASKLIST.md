# Firmware Improvements — Task List

Actionable checklist derived from [`FIRMWARE_UPDATE_STRATEGY.md`](./FIRMWARE_UPDATE_STRATEGY.md).
Work top to bottom. Each top-level item is meant to be its own git commit
(or short commit sequence) on the `firmware/local-improvements` branch, so
any one of them can be reverted or cherry-picked independently.

Related docs:
- [`FIRMWARE_IMPROVEMENTS.md`](./FIRMWARE_IMPROVEMENTS.md) — the 9 proposed improvements (source of truth for *what*)
- [`FIRMWARE_UPDATE_STRATEGY.md`](./FIRMWARE_UPDATE_STRATEGY.md) — the *why* and architecture decisions
- [`FIRMWARE_REMAPPER_SUPPORT.md`](./FIRMWARE_REMAPPER_SUPPORT.md) — separate, related constant-naming work for the remapper tool

---

## Phase 0 — Baseline & branch setup

- [ ] Confirm `main` == `upstream/main` for the firmware file:
      `git fetch upstream` then
      `git diff upstream/main main -- Firmware/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino`
      (should be empty right now)
- [ ] Create the long-lived branch: `git checkout -b firmware/local-improvements upstream/main`
- [ ] Create `FirmwareTuning/scripts/` and `FirmwareTuning/tests/` folders
- [ ] Add `scripts/check-upstream-diff.ps1` (diffs the `.ino` against `upstream/main`, prints a clear report)
- [ ] Add `scripts/build-firmware.ps1` (wraps `arduino-cli compile` for the Pro Micro / ATmega32U4 FQBN)
- [ ] Add `scripts/run-logic-tests.ps1` (compiles + runs the PC-side `orbit_logic.h` unit tests with g++)
- [ ] Verify `arduino-cli` is installed (or document the fallback: Arduino IDE manual verify) and record the board FQBN used

---

## Phase 1 — Non-behavioral restructuring (must compile identically before any new logic is added)

Do these as small, individually-verified commits. After **each** file
extraction, run `build-firmware.ps1` and confirm the compiled sketch is
functionally unchanged (manual hardware smoke test once at the end of the
phase is enough — don't need full hardware test after every single move).

- [ ] Extract `orbit_logic.h`: `smoothValue`, `applyGain`, `applyResponseCurve`,
      `applyInputDeadzone`, `applyOutputDeadzone`, `keepOnlyDominantAxis`,
      `countPositive4`, `countNegative4` — no Arduino dependencies
- [ ] Extract `orbit_buttons.h`: `readButtonMask`, `getModeSwitchButtonMask`,
      `getSlicerModeButtonMask`, `isModeSwitchComboPressed`,
      `isSlicerModeComboPressed`, `filterModeSwitchButtons`,
      `filterSlicerModeButtons`, `resetModeSwitchChord`, chord state variables
- [ ] (Optional polish) Extract `orbit_slicer_hid.h`: the `SlicerMouseHID_` class
      and its report descriptors
- [ ] (Optional polish) Extract `orbit_hid_descriptors.h`: the three
      `hidReportDescriptor` / `mouseReportDescriptor` / `keyboardReportDescriptor`
      PROGMEM arrays
- [ ] Confirm the `.ino` still contains, unmoved, every top-level `const`
      that `Configurator/tuning.html` and `ButtonRemapper` parse (spot-check
      both tools against the restructured `.ino`)
- [ ] Run full manual hardware checklist once (see Phase 4) to confirm zero
      behavior change from restructuring alone
- [ ] Commit as: `firmware: extract pure logic and button/chord code into headers (no behavior change)`

---

## Phase 2 — Implement the 9 improvements, in priority order

Each item: implement → add/extend PC-side unit tests if logic-only → run
`run-logic-tests.ps1` → run `build-firmware.ps1` → commit.

- [ ] **1. Button debounce** (`orbit_buttons.h`)
  - [ ] Add `BUTTON_DEBOUNCE_MS`, debounced mask state, `readDebouncedButtons()`
  - [ ] Wire into `loop()` in place of the raw `readButtonMask()` call
  - [ ] Unit test: mask change shorter than `BUTTON_DEBOUNCE_MS` is ignored; sustained change is accepted
  - [ ] Hardware test: rapid button taps no longer cause spurious mode/slicer toggles

- [ ] **2. EEPROM persistence** (`orbit_eeprom.h`, new)
  - [ ] Add `EEPROM_ADDR_SPEED_MODE`, `EEPROM_ADDR_SLICER_MODE`, `EEPROM_MAGIC_ADDR`, `EEPROM_MAGIC_VALUE`
  - [ ] Load in `setup()`, save on change via `EEPROM.update()`
  - [ ] Hardware test: change speed mode + slicer mode, power-cycle, confirm both restored

- [ ] **3. LED feedback** (`orbit_leds.h`, new)
  - [ ] Non-blocking (millis-based) TX blink pattern per speed mode; RX solid/off for slicer mode
  - [ ] Wire calls into `updateSpeedMode()` / `updateSlicerMouseMode()`
  - [ ] Hardware test: LED patterns match the table in `FIRMWARE_IMPROVEMENTS.md` §5, and HID reports are never delayed by LED timing

- [ ] **4. Named constants for magic numbers** (`.ino` SETTINGS block)
  - [ ] Add `ROTATION_PRIORITY_THRESHOLD`, `Z_PUSHPULL_THRESHOLD_MULT`, `Z_ROTATION_THRESHOLD_MULT`, `Z_ROTATION_DIVISOR`
  - [ ] Replace the literals (`80`, `* 2`, `* 3`, `/ 2`) in `loop()` with the new constants
  - [ ] Confirm behavior is byte-for-byte identical (these are pure renames)

- [ ] **5. Calibration sanity check** (`.ino` or new `orbit_calibration.h` if it grows)
  - [ ] Add `CALIBRATION_MIN` / `CALIBRATION_MAX`
  - [ ] After `calibrateCenter()`, validate each of the 8 centers against the range
  - [ ] Decide + implement the failure indication (reuse `orbit_leds.h` blink pattern, and/or block HID output until recalibrated)
  - [ ] Unit test: centers inside range pass; centers outside range are flagged
  - [ ] Hardware test: intentionally miswire/disconnect one joystick, confirm the flag fires

- [ ] **6. Hardcoded array sizes → `BUTTON_COUNT`**
  - [ ] Replace literal `3` in `slicerButtonWasPressed`, `slicerButtonLongHandled`,
        `slicerButtonPressedAt`, `SLICER_BUTTON_ACTIONS` with `BUTTON_COUNT`
  - [ ] Confirm `BUTTON_COUNT` is available wherever these arrays are now declared (may require moving the constant earlier, or into a shared header)

- [ ] **7. Logic/hardware separation**
  - [ ] Already substantially done by Phase 1's `orbit_logic.h` extraction — this item is to confirm full coverage and backfill any pure function still left inline in the `.ino`
  - [ ] Confirm `orbit_logic.h` has zero `#include <Arduino.h>` or hardware calls

- [ ] **8. Fixed-point math (conditional — only if profiling shows a need)**
  - [ ] Profile first: `micros()` before/after `loop()` under `DEBUG_SERIAL`, record headroom
  - [ ] If (and only if) headroom is a concern: convert `applyGain`/`applyResponseCurve` to fixed-point (×256 scale) in `orbit_logic.h`, replace `pow()` with a lookup table or piecewise-linear approximation
  - [ ] Unit test: fixed-point results match the old float results within an acceptable rounding tolerance across a representative input sweep

- [ ] **9. Chord membership documentation / simplification**
  - [ ] At minimum: add the explicit priority-rule comment block to `orbit_buttons.h` (slicer toggle fires only on exact 2+3, mode switch fires only on all 3) as described in `FIRMWARE_IMPROVEMENTS.md` §3
  - [ ] Treat actual hardware/wiring changes (exclusive buttons per chord) as a separate, optional, higher-risk follow-up — not required for this pass

---

## Phase 3 — Remapper compatibility pass (from `FIRMWARE_REMAPPER_SUPPORT.md`)

Independent of the 9 improvements, but touches the same file — do it as
its own commit(s) so it can be reviewed/reverted separately.

- [ ] Replace `SLICER_SHORTCUT_MODIFIER_PRIMARY` with the six per-slot constants
      (`..._HOME_SHORT`, `..._HOME_LONG`, `..._PAINT_SHORT`, `..._PAINT_LONG`,
      `..._TAB_SHORT`, `..._TAB_LONG`)
- [ ] Update `runSlicerButtonAction()` to use the new constants
- [ ] Update `ButtonRemapper`'s `ACTION_SHORTCUTS` and `SHORTCUT_CONSTANTS` to match
- [ ] Manual test: open the remapper against the updated `.ino`, confirm every slot shows a modifier dropdown and round-trips correctly

---

## Phase 4 — Validation & release

- [ ] Run `run-logic-tests.ps1` — all PC-side unit tests pass
- [ ] Run `build-firmware.ps1` — compiles cleanly for the Pro Micro / ATmega32U4 target
- [ ] Manual hardware checklist:
  - [ ] Startup calibration completes and sanity-check correctly passes/flags
  - [ ] Each individual button registers correctly
  - [ ] Mode-switch chord (all 3 buttons) cycles speed modes; debounce prevents double-cycling
  - [ ] Slicer-mode chord (buttons 2+3) toggles slicer mouse mode
  - [ ] Speed mode and slicer mode both survive a power cycle (EEPROM)
  - [ ] TX/RX LED feedback matches the documented pattern for every mode
  - [ ] `Configurator/tuning.html` opens, edits, and saves the updated `.ino` without errors
  - [ ] `ButtonRemapper` opens, edits, and saves the updated `.ino` without errors
- [ ] Copy the final sketch folder into `FirmwareUpdates/vX.Y.Z/`
- [ ] Add a `CHANGELOG.md` entry summarizing the shipped improvements
- [ ] Tag the release: `git tag firmware-vX.Y.Z`
- [ ] (Optional) Export the improvement branch as a patch series for the record:
      `git format-patch upstream/main..firmware/local-improvements -o FirmwareUpdates/patches`

---

## Ongoing — when upstream changes the base `.ino`

- [ ] `git fetch upstream`
- [ ] Run `scripts/check-upstream-diff.ps1`
- [ ] If diverged: `git checkout firmware/local-improvements && git rebase upstream/main`
- [ ] Resolve conflicts commit-by-commit (each commit = one improvement, so conflicts are easy to attribute)
- [ ] Re-run Phase 4 validation in full before re-tagging a release
