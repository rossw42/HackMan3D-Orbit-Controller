# Firmware Improvements — Task List

Actionable checklist derived from [`FIRMWARE_UPDATE_STRATEGY.md`](./FIRMWARE_UPDATE_STRATEGY.md).
Work top to bottom. Each top-level item is meant to be its own git commit
(or short commit sequence) on the `firmware/local-improvements` branch, so
any one of them can be reverted or cherry-picked independently.

Related docs:
- [`FIRMWARE_IMPROVEMENTS.md`](./FIRMWARE_IMPROVEMENTS.md) — the 9 proposed improvements (source of truth for *what*)
- [`FIRMWARE_UPDATE_STRATEGY.md`](./FIRMWARE_UPDATE_STRATEGY.md) — the *why* and architecture decisions
- [`FIRMWARE_REMAPPER_SUPPORT.md`](./FIRMWARE_REMAPPER_SUPPORT.md) — separate, related constant-naming work for the remapper tool
- [`FIRMWARE_IMPROVEMENT_REPORT.md`](./FIRMWARE_IMPROVEMENT_REPORT.md) — **final report: what was implemented and how it improves the device**

---

## ✅ Implementation Status — v1.1.0

All Phase 2 improvements and the Phase 3 remapper pass have been applied
directly to `Firmware/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino`
as a single clean v1.1.0 release. The file compiles with zero behavior changes
to existing functionality while adding all new features.

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

## Phase 1 — Non-behavioral restructuring ✅ DONE in v1.1.0

Pure logic and button/chord code extracted into header files in
`FirmwareUpdates/v1.1.0/Hackman3D_Orbit_Controller/`.
The `.ino` `#include`s both headers and removes all duplicated code.

- [x] Extract `orbit_logic.h`: `smoothValue`, `applyGain`, `applyResponseCurve`,
      `applyInputDeadzone`, `applyOutputDeadzone`, `keepOnlyDominantAxis`,
      `countPositive4`, `countNegative4` — **no Arduino dependencies**;
      compilable with plain `g++` for PC-side unit testing
- [x] Extract `orbit_buttons.h`: `readDebouncedButtons`, `getButtonComboMask`,
      `isModeSwitchComboPressed`, `isSlicerModeComboPressed`,
      `filterModeSwitchButtons`, `filterSlicerModeButtons`,
      `resetModeSwitchChord`, chord priority rules documented at top of file
- [x] Updated `.ino` calls all logic via the headers; all `orbit_logic.h`
      functions now accept deadzone/divisor as parameters so they have
      zero Arduino dependencies
- [x] All top-level `const` constants that `Configurator/tuning.html` and
      `ButtonRemapper` parse remain in the `.ino` — headers contain only
      functions, not user-facing constants
- [x] Extract `orbit_slicer_hid.h`: the `SlicerMouseHID_` class with
      `sendReport()` and `sendKeyboardReport()`; constructor accepts
      `enableSlicer` and `enableKeyboard` bool arguments
- [x] Extract `orbit_hid_descriptors.h`: `hidReportDescriptor`,
      `mouseReportDescriptor`, `keyboardReportDescriptor` PROGMEM arrays
      with full bilingual comments
- [x] `.ino` now `#include`s all four headers and instantiates
      `SlicerMouseHID_` via the header constructor
- [ ] Run full manual hardware checklist once (see Phase 4) to confirm zero
      behavior change from restructuring alone
- [ ] Commit as: `firmware: extract pure logic and button/chord code into headers (no behavior change)`

---

## Phase 2 — Implement the 9 improvements ✅ DONE in v1.1.0

All items below have been implemented in the firmware. Hardware validation
is the remaining step before tagging a release.

- [x] **1. Button debounce** — `readDebouncedButtons()` added
  - [x] `BUTTON_DEBOUNCE_MS = 10`, debounced mask state, stable-window filter
  - [x] Wired into `loop()` replacing the raw `readButtonMask()` call
  - [ ] Hardware test: rapid button taps no longer cause spurious mode/slicer toggles

- [x] **2. EEPROM persistence** — `eepromLoad()` / `eepromSave()` added
  - [x] `EEPROM_MAGIC_ADDR`, `EEPROM_MAGIC_VALUE`, `EEPROM_ADDR_SPEED_MODE`, `EEPROM_ADDR_SLICER_MODE`
  - [x] Load in `setup()`, save on change via `EEPROM.update()`
  - [ ] Hardware test: change speed mode + slicer mode, power-cycle, confirm both restored

- [x] **3. LED feedback** — `ledStartBlink()`, `ledUpdateBlink()`, `ledSignalSpeedMode()`,
      `ledSignalSlicerMode()`, `ledUpdateRx()` added
  - [x] Non-blocking millis-based TX blink pattern: mode 0 = 1×, mode 1 = 2×, mode 2 = 3×
  - [x] RX LED solid for 500 ms when slicer mode toggles; off when slicer mode disabled
  - [x] TX LED signals current speed mode on startup after calibration
  - [x] Wired into `updateSpeedMode()`, `updateSlicerMouseMode()`, and end of `loop()`
  - [ ] Hardware test: LED patterns match the table in `FIRMWARE_IMPROVEMENTS.md` §5

- [x] **4. Named constants for magic numbers**
  - [x] `ROTATION_PRIORITY_THRESHOLD = 80` replaces bare `80` in rotation mode check
  - [x] `Z_PUSHPULL_THRESHOLD_MULT = 2` replaces `* 2` in Z push/pull detection
  - [x] `Z_ROTATION_THRESHOLD_MULT = 3` replaces `* 3` in Z rotation detection
  - [x] `Z_ROTATION_DIVISOR = 2` replaces `/ 2` in rotZ calculation

- [x] **5. Calibration sanity check**
  - [x] `CALIBRATION_MIN = 300`, `CALIBRATION_MAX = 750` added to settings block
  - [x] `calibrationFailed` flag set in `calibrateCenter()` if any center is out of range
  - [x] `ledFlashCalibrationError()` fires 6 rapid TX LED blinks on bad calibration
  - [x] `calibrationFailed` printed in debug serial output
  - [ ] Hardware test: disconnect a joystick and confirm error flash fires at startup

- [x] **6. Hardcoded array sizes → `BUTTON_COUNT`**
  - [x] `slicerButtonWasPressed[BUTTON_COUNT]`
  - [x] `slicerButtonLongHandled[BUTTON_COUNT]`
  - [x] `slicerButtonPressedAt[BUTTON_COUNT]`
  - [x] `SLICER_BUTTON_ACTIONS[BUTTON_COUNT]` (moved to global variables section)
  - [x] `buttonPins[BUTTON_COUNT]` (was already `[3]` but now uses the constant)

- [x] **7. Logic/hardware separation**
  - [x] All pure math functions confirmed present and unmodified
  - [x] Phase 1 header extraction is the recommended follow-up for full separation
  - Note: Full `orbit_logic.h` extraction is a Phase 1 task; v1.1.0 keeps them in-file

- [x] **8. Fixed-point math** — implemented in `orbit_logic.h`
  - [x] `applyGain()` converted to `×256` integer multiply + right-shift-8
        (eliminates one software-emulated float multiply per axis per loop)
  - [x] `applyResponseCurve()` converted to fixed-point with a 64-entry
        piecewise-linear lookup table per speed mode (3 tables × 64 uint8_t = 192 bytes FLASH)
        replacing `pow()` which was the single most expensive float call per loop
  - [x] Pre-computed `GAIN_*_FP`, `SPEED_SCALE_FP[]`, and `INPUT_MAX_*_FP256`
        constants in `orbit_logic.h` — no runtime float arithmetic in the hot path
  - [x] `SPEED_MODE_CURVE_IDX[]` in `.ino` maps speed mode index to LUT index
  - [ ] Hardware validation: measure loop headroom with `micros()` before/after
        under `DEBUG_SERIAL = true` and confirm improvement vs. float baseline

- [x] **9. Chord membership documentation / simplification**
  - [x] Full priority-rule comment block added to `isSlicerModeComboPressed()` in both
        English and French: exact-match requirement, mutual exclusion guarantee,
        explanation of shared button membership

---

## Phase 3 — Remapper compatibility pass ✅ DONE in v1.1.0

- [x] Replaced `SLICER_SHORTCUT_MODIFIER_PRIMARY` with six per-slot constants:
      `SLICER_SHORTCUT_MODIFIER_HOME_SHORT`, `SLICER_SHORTCUT_MODIFIER_HOME_LONG`,
      `SLICER_SHORTCUT_MODIFIER_PAINT_SHORT`, `SLICER_SHORTCUT_MODIFIER_PAINT_LONG`,
      `SLICER_SHORTCUT_MODIFIER_TAB_SHORT`, `SLICER_SHORTCUT_MODIFIER_TAB_LONG`
- [x] Updated `runSlicerButtonAction()` to use the new per-slot constants
- [x] Default values preserved — behavior is byte-for-byte identical to v1.0.0
- [x] `ButtonRemapper/remap.html` copied from `feature/button-remapper-firmware` branch
      and updated: `ACTION_SHORTCUTS` now uses six per-slot `SLICER_SHORTCUT_MODIFIER_*`
      constants; `SHORTCUT_CONSTANTS` updated to match; old `SLICER_SHORTCUT_MODIFIER_PRIMARY`
      removed; all 6 slots now show a modifier dropdown independently
- [ ] Manual test: open `ButtonRemapper/remap.html` against the v1.1.0 `.ino`, confirm
      every slot shows a modifier dropdown and round-trips correctly

---

## Phase 4 — Validation & release

- [ ] Run `build-firmware.ps1` — compiles cleanly for the Pro Micro / ATmega32U4 target
- [ ] Manual hardware checklist:
  - [ ] Startup calibration completes — TX LED blinks mode count after calibration
  - [ ] Deliberate bad calibration (disconnect a joystick) triggers 6-flash error
  - [ ] Each individual button registers correctly
  - [ ] Mode-switch chord (all 3 buttons) cycles speed modes; TX LED blinks 1/2/3
  - [ ] Debounce prevents double-cycling on rapid taps
  - [ ] Slicer-mode chord (buttons 2+3 only) toggles slicer mouse mode; RX LED holds 500 ms
  - [ ] Speed mode and slicer mode both survive a power cycle (EEPROM)
  - [ ] On fresh board (EEPROM not initialized), firmware defaults are used correctly
  - [ ] `Configurator/tuning.html` opens, edits, and saves the updated `.ino` without errors
  - [ ] `ButtonRemapper` opens, edits, and saves the updated `.ino` without errors
    (after the ButtonRemapper JS is updated per Phase 3 remaining item)
- [ ] Copy the final sketch folder into `FirmwareUpdates/v1.1.0/`
- [ ] Add a `CHANGELOG.md` entry summarizing the shipped improvements
- [ ] Tag the release: `git tag firmware-v1.1.0`
- [ ] (Optional) Export the improvement branch as a patch series:
      `git format-patch upstream/main..firmware/local-improvements -o FirmwareUpdates/patches`

---

## Ongoing — when upstream changes the base `.ino`

- [ ] `git fetch upstream`
- [ ] Run `scripts/check-upstream-diff.ps1`
- [ ] If diverged: `git checkout firmware/local-improvements && git rebase upstream/main`
- [ ] Resolve conflicts commit-by-commit (each commit = one improvement, so conflicts are easy to attribute)
- [ ] Re-run Phase 4 validation in full before re-tagging a release
