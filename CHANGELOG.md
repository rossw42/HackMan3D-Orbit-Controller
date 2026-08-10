# Changelog

All notable changes to HackMan3D Orbit Controller are documented here.

The project follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- **Calibration Wizard** (`Configurator/calibrate.html`) — a guided WebHID browser tool
  that walks the user through 13 step-by-step joystick movements (hands-off rest,
  left/right, forward/back, up/down, twist CW/CCW, and four tilts) to measure
  per-device sensor ranges, noise floor, and axis directions.

  - Works with the **QMK `viam` keymap** (no firmware changes required beyond flashing
    `viam` once). Connects to both the SpaceMouse HID interface (live axis readings) and
    the VIA Raw HID interface (writing calibration to EEPROM).
  - Auto-captures each movement step by watching **all six axes** simultaneously —
    handles per-device axis mapping differences where, for example, "push left" may
    arrive on a different axis than the pipeline default.
  - Computes a noise-derived `deadzone_input`, per-axis `gain_fp[]` correction factors
    (×256 fixed-point, clamped 0.25×–4×), and axis inversion suggestions.
  - **Save to device** writes `deadzone_input`, `gain_fp[0..5]`, and `invert_mask` bits
    directly to EEPROM via VIA channel 0 + `id_custom_save`, then fires `id_recalibrate`
    for an immediate center recapture. The saved values are immediately visible in VIA's
    Axes tab and persist across power cycles via `orbit_config_load()` at boot.
  - **Factory reset** restores firmware compiled defaults via `id_reset_defaults`.
  - Includes a "Before you start" prerequisite table with the flash command and a note
    about returning to Arduino firmware.

- **Firmware v1.2.0** (`FirmwareUpdates/v1.2.0/`) — Arduino firmware upgrade adding a
  serial calibration protocol for devices staying on the Arduino path (not needed for
  the QMK wizard, kept as a parallel option):
  - New `orbit_serial.h`: versioned + XOR-checksummed `OrbitCalibration` EEPROM block
    at address 16 (compatible with v1.1.0 settings at 0–2), load/save/erase helpers,
    and clamped fixed-point range-normalization gain math.
  - Line-based USB-CDC protocol: `I` identify, `R` raw ADC stream (~50 Hz), `C` capture
    center, `D` dump, `W` write 26-value calibration, `F` factory reset.
  - Boot prefers stored calibration (fixes "knob bumped during boot" mis-center), falls
    back to classic startup center capture when no valid block exists.
  - Runtime deadzone and per-channel gains replace the compile-time constants in the
    main loop.

### Fixed

- **Axes no longer go dead at full deflection** (`orbit_logic.h`). The three `CURVE_TABLE_*`
  response-curve tables were declared `uint8_t`, but their last eight entries are the literal
  `256`, which does not fit in a byte and silently wrapped to `0`. Past roughly **88 % of
  joystick deflection** an axis therefore stopped increasing and instead dropped to the output
  deadzone — push a stick to its mechanical limit and that axis went dead. `avr-gcc` had been
  emitting eight `-Woverflow` warnings about this all along.

  The tables are now `uint16_t` (and `lookupCurve()` / `applyResponseCurve()` updated to
  match). **All 192 table entries are byte-for-byte unchanged**, so response below ~88 %
  deflection is bit-identical to before; only the top ~14 % of travel changes, from dead to
  full-scale. Costs 192 bytes of flash; RAM unaffected.

  Note the tables were *not* regenerated. Their comment claimed `pow(i/63, curve)`, but the
  data actually fits `pow(i/56, curve)` — full output at 88.9 % deflection with a flat top
  ~11 % is deliberate, so you don't have to bottom out the stick for full speed. Regenerating
  from `i/63` would have moved saturation to 100 % and made the device feel *slower*. The
  comment has been corrected instead.

  Regression test: `qmk/test/lut_fix_verify.c`.

### Added

- **Combined Configurator** (`Configurator/HackMan3D_Orbit_Controller-configurator.html`) — a single browser-based tool
  that merges the previous `Configurator/tuning.html` and `ButtonRemapper/remap.html` tools into one tabbed interface
  (⚙ Tuning / ⌨ Button Remapping) with a single "Save all changes to .ino" action and shared backup-on-save behavior.
- New v1.1.0 firmware parameters exposed in the Tuning tab: `CALIBRATION_MIN`, `CALIBRATION_MAX`,
  `ROTATION_PRIORITY_THRESHOLD`, `Z_PUSHPULL_THRESHOLD_MULT`, `Z_ROTATION_THRESHOLD_MULT`, `Z_ROTATION_DIVISOR`,
  `SLICER_MODE_HOLD_MS`, `SLICER_MODE_DEBOUNCE_MS`, `BUTTON_DEBOUNCE_MS`, `LED_BLINK_ON_MS`, `LED_BLINK_OFF_MS`,
  `RX_LED_HOLD_MS`.
- Firmware-version badge and an in-app warning banner that detects fixed-point (`orbit_logic.h`) firmware builds and
  explains which sliders are documentation-only on that firmware (gains, max speed scale, response curve, speed
  mode profiles) versus which sections work normally on-device.

### Changed

- **Renamed slicer button shortcut constants** (v1.1.0 firmware, `ButtonRemapper/remap.html`, and the combined
  configurator) from an action-based scheme to a generic, per-physical-button scheme:
  - Before: `SLICER_BUTTON_ACTION_HOME/PAINT/TAB_SEND` + `SLICER_BUTTON_ACTIONS[]` + six
    `SLICER_SHORTCUT_MODIFIER_HOME_SHORT/LONG` / `_PAINT_*` / `_TAB_*` constants.
  - After: twelve generic constants directly keyed by physical button number (matching the "Button 1/2/3"
    badges shown in the UI) — `SLICER_SHORTCUT_MODIFIER_BUTTON{1,2,3}_{SHORT,LONG}` and
    `SLICER_SHORTCUT_KEY_BUTTON{1,2,3}_{SHORT,LONG}`.
  - Removes the extra "action" indirection layer entirely — no code or UI needs to track which action a
    button performs, only which button/press is being edited.

### Documentation

- **`qmk/` — QMK port research and plan.** Seven documents covering the full port of the
  v1.1.0 firmware to QMK with live keymap editing and live parameter tuning: a complete
  firmware inventory (every tunable parameter, the HID descriptors, the fixed-point math, the
  chord state machine, a 30-row behaviour checklist), target architecture, the 3Dconnexion
  multi-axis HID descriptor patch QMK requires, a VIA custom-menu design exposing 50
  live-editable values, a measured flash/RAM/EEPROM budget, and an 11-phase task list.
  Measured on ATmega32U4 (28,672 B budget): 10,574 B without VIA, 11,920 B with — so VIA costs
  only 1,346 B and no MCU change is needed.
- **`qmk/test/` — host-side verification harness.** `lut_bug_check.c` (documents the curve-LUT
  bug), `lut_fix_verify.c` (regression test for the fix), and `input_max_check.c` (validates
  all six `INPUT_MAX_*` constants against the real pipeline).
- `orbit_logic.h` comments corrected: the curve tables normalise on `i/56`, not `i/63`; and the
  `INPUT_MAX_RZ = 1024 × gain` vs `INPUT_MAX_TZ = 2048 × gain` asymmetry is **correct**,
  because `rotZ` is divided by `Z_ROTATION_DIVISOR`. Raising RZ to 2048 would cost that axis
  roughly two thirds of its output range.
- `Configurator/README.md` rewritten to document the combined configurator, firmware-version detection, the
  fixed-point firmware notice, and the new generic per-button shortcut naming.
- Legacy `tuning.html` and `remap.html` tools remain in the repository for reference but are superseded by the
  combined configurator. `remap.html` itself was updated in place to use the new generic constant names.


## [1.0.0] - 2026-07-26


### Added

- Stable Arduino Pro Micro firmware for six-axis HID navigation
- Three selectable speed profiles
- Adjustable gain, dead zones, smoothing, response curves, and axis inversion
- Optional slicer mouse-emulation mode and keyboard shortcuts
- Assembly guide, Bill of Materials, wiring diagrams, and tuning guide
- Windows, macOS, and Linux compatibility documentation

### Documentation

- Added a guided quick-start workflow
- Added structured compatibility, hardware, control, and troubleshooting sections
- Added contribution, security, issue, and pull-request guidance

[1.0.0]: https://github.com/HackMan3D/HackMan3D-Orbit-Controller/releases/tag/v1.0.0
