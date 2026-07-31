# Combined Configurator — Task List

Tracks the work to merge `Configurator/tuning.html` and `ButtonRemapper/remap.html` into a single
tool: `Configurator/HackMan3D_Orbit_Controller-configurator.html`.

Related docs:
- [`CONFIGURATOR_MERGE_IMPROVEMENTS.md`](./CONFIGURATOR_MERGE_IMPROVEMENTS.md) — what changed and why
- [`../Configurator/README.md`](../Configurator/README.md) — end-user documentation for the combined tool
- [`../CHANGELOG.md`](../CHANGELOG.md) — project-level changelog entry

---

## Research

- [x] Read `ButtonRemapper/remap.html` in full (structure, CSS, JS, key/modifier tables, parse/patch logic)
- [x] Read `Configurator/tuning.html` in full (structure, CSS, JS, parameter list, parse/patch logic)
- [x] Read `Firmware/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino` (v1.0.0, shipped) in full
- [x] Read `FirmwareUpdates/v1.1.0/Hackman3D_Orbit_Controller/*.{ino,h}` (v1.1.0, unreleased refactor) in full
- [x] Diff v1.0.0 vs v1.1.0 to identify every NEW constant not yet exposed in `tuning.html`
- [x] Identify firmware constants that exist in the `.ino` but no longer affect behavior on v1.1.0
      (fixed-point migration: `GAIN_*`, `MAX_SPEED_SCALE`, `RESPONSE_CURVE`, `SPEED_MODE_SCALE[]`,
      `SPEED_MODE_RESPONSE_CURVE[]` — real values now live in `orbit_logic.h` as `GAIN_*_FP`,
      `SPEED_SCALE_FP[]`, `CURVE_TABLE_*`)

## Build

- [x] Create `Configurator/HackMan3D_Orbit_Controller-configurator.html`
  - [x] Tabbed layout: **⚙ Tuning** / **⌨ Button Remapping**
  - [x] Port all `tuning.html` sections (dead zones, smoothing/calibration, speed & response,
        translation/rotation gains, axis behavior, speed mode profiles, speed-mode button shortcut,
        slicer mouse mode, serial debug)
  - [x] Add new v1.1.0-only sections/rows: `CALIBRATION_MIN`/`CALIBRATION_MAX`,
        `ROTATION_PRIORITY_THRESHOLD`, Z-axis gesture detection (`Z_PUSHPULL_THRESHOLD_MULT`,
        `Z_ROTATION_THRESHOLD_MULT`, `Z_ROTATION_DIVISOR`), `SLICER_MODE_HOLD_MS`/`SLICER_MODE_DEBOUNCE_MS`,
        Button debounce (`BUTTON_DEBOUNCE_MS`), LED feedback (`LED_BLINK_ON_MS`, `LED_BLINK_OFF_MS`,
        `RX_LED_HOLD_MS`) — all marked with a **NEW** badge in the UI
  - [x] Port all `remap.html` logic (key/modifier tables, per-action shortcut constants, button cards,
        short/long press rows, live preview)
  - [x] Namespace tuning functions with `tn` prefix and remap functions with `rm` prefix to avoid
        collisions in the merged `<script>` block
  - [x] Single shared `openFile()` / `onSaveAll()` / backup-on-save flow for both tabs
  - [x] Per-tab **Discard changes**; Tuning tab also gets **Reset tuning to defaults**
  - [x] Firmware-version badge (parsed from the `Version:` header comment)
  - [x] Fixed-point firmware detection (`orbit_logic.h` / `GAIN_TX_FP` sniff) + warning banner explaining
        which Tuning sections are display-only on v1.1.0+ firmware

## Verification

- [x] Extracted all pure parse/patch functions (`tnParseState`, `tnApplyPatch`, `rmParseState`,
      `rmApplyPatch`) from the shipped HTML into a Node `vm` sandbox and ran them against both
      real firmware files:
      - `Firmware/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino` (v1.0.0)
      - `FirmwareUpdates/v1.1.0/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino` (v1.1.0)
- [x] Confirmed round-trip parse → mutate → patch → re-parse produces the expected values for
      dead zones, gains, booleans, and speed-mode arrays on v1.0.0
- [x] Confirmed the same round-trip on v1.1.0 for all parameters that still exist as real constants
      (dead zones, calibration, Z-axis gestures, button debounce, LED feedback, slicer mouse mode, etc.)
- [x] Confirmed the known/expected no-op behavior on v1.1.0 for `GAIN_*`, `MAX_SPEED_SCALE`,
      `RESPONSE_CURVE`, and `SPEED_MODE_SCALE[]`/`SPEED_MODE_RESPONSE_CURVE[]` (values are written to
      the `.ino` but do not affect firmware behavior — this is documented in the warning banner, not a bug)
- [x] Confirmed button-remap round-trip patch (short/long modifier + key constants) on both firmware
      versions, including v1.0.0's fallback where per-slot modifier constants don't exist
- [x] Confirmed combined save-all patch (tuning + remap together) does not interfere between the two
      patch passes on either firmware version
- [x] Launched the combined HTML in the browser to confirm it loads with no console errors
- [x] Removed temporary Node test harness scripts after verification

## Documentation

- [x] Rewrote `Configurator/README.md` for the combined tool (usage, firmware detection, fixed-point
      notice, full parameter tables, button remapping tab, save behavior, legacy tools note)
- [x] Added `[Unreleased]` section to root `CHANGELOG.md` describing the combined configurator and new
      exposed parameters
- [x] Created this task list and the companion improvements doc

## Button shortcut constant rename (generic per-button naming)

- [x] Reviewed the firmware's slicer shortcut constants and identified the action-based indirection
      (`SLICER_BUTTON_ACTION_HOME/PAINT/TAB_SEND` + `SLICER_BUTTON_ACTIONS[BUTTON_COUNT]` +
      `SLICER_SHORTCUT_MODIFIER_HOME_SHORT/LONG`, `_PAINT_*`, `_TAB_*`) as unnecessarily indirect —
      renamed per user request to a generic, physical-button-numbered scheme
- [x] Renamed 12 constants in `FirmwareUpdates/v1.1.0/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino`:
      `SLICER_SHORTCUT_MODIFIER_BUTTON{1,2,3}_{SHORT,LONG}` and `SLICER_SHORTCUT_KEY_BUTTON{1,2,3}_{SHORT,LONG}`,
      preserving all default values byte-for-byte
- [x] Removed the now-unused `SLICER_BUTTON_ACTION_HOME/PAINT/TAB_SEND`, `SLICER_BUTTON_ACTIONS[]`,
      `SLICER_SHORTCUT_MODIFIER_SHIFT`, and the five original `SLICER_SHORTCUT_KEY_0/A/G/L/N/TAB` constants;
      replaced `runSlicerButtonAction()` to take a `buttonIndex` directly instead of an `action` enum, using
      new `SLICER_SHORTCUT_MODIFIER_SHORT[]`/`_LONG[]` and `SLICER_SHORTCUT_KEY_SHORT[]`/`_LONG[]` lookup tables
- [x] Updated `ButtonRemapper/remap.html` to parse/patch the new generic constants directly (no more
      `ACTION_CODE`/`ACTION_SHORTCUTS`/`buttonActions` indirection); `BUTTON_SHORTCUTS[]` now maps button
      index 0-2 straight to its four constant names
- [x] Updated the combined configurator's remap module (`rm*` functions) the same way; removed
      `ACTION_DISPLAY_NAME` badge label and the `buttonActions` array from `remapDraft`/`remapBaseline`
- [x] Updated `Configurator/README.md` Button Remapping section and info panel text to describe the new
      generic per-button constants instead of the action-based ones
- [x] Verified via Node `vm` sandbox against the real v1.1.0 `.ino`: all 12 constants parse correctly in
      both the combined configurator and standalone `remap.html`; independent round-trip patch of two
      different buttons' constants leaves the other 10 untouched; confirmed the v1.0.0 firmware (which
      predates this constant scheme entirely) is left byte-for-byte unchanged by the remap patch (safe no-op,
      not a crash or silent corruption)
- [x] Re-ran the full tuning + remap + combined save-all regression suite after the rename — all tuning
      tests and v1.1.0 remap/combined tests pass; v1.0.0 remap is a correct no-op as expected
- [x] Added a `[Unreleased] → Changed` entry to `CHANGELOG.md` documenting the rename

## Follow-ups (not done in this pass)


- [ ] Manual hardware-adjacent test: open a real device-flashed `.ino` copy in Chrome/Edge via the file
      picker (cannot be automated in this environment — file pickers require a real user gesture)
- [ ] Consider adding an "Edit orbit_logic.h" helper/link or a second file-open slot so v1.1.0 users can
      tune the *real* fixed-point gain/speed constants from the same tool
- [ ] Consider removing/archiving the legacy `tuning.html` and `remap.html` once the combined tool has
      been validated on hardware
