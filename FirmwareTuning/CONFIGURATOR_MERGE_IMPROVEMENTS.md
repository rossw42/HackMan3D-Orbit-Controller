# Combined Configurator — Improvements Report

Summary of what changed when `Configurator/tuning.html` and `ButtonRemapper/remap.html` were merged
into `Configurator/HackMan3D_Orbit_Controller-configurator.html`, and why.

See [`CONFIGURATOR_MERGE_TASKLIST.md`](./CONFIGURATOR_MERGE_TASKLIST.md) for the step-by-step checklist.

---

## 1. Why merge

Prior to this change, tuning parameters and button remapping lived in two separate single-purpose HTML
tools that both edited the same `.ino` file independently:

- `Configurator/tuning.html` — sliders/toggles for firmware constants.
- `ButtonRemapper/remap.html` — dropdowns for slicer keyboard shortcuts.

Users had to open the `.ino` twice, in two different tools, and could lose track of which tool's backup
was the most recent. Neither tool exposed the parameters added by the (unreleased) firmware v1.1.0
refactor in `FirmwareUpdates/v1.1.0/`.

## 2. What changed

### 2.1 Single tabbed tool

`HackMan3D_Orbit_Controller-configurator.html` opens the `.ino` file once and presents two tabs:

- **⚙ Tuning** — identical section layout to the old `tuning.html`, plus new v1.1.0 sections.
- **⌨ Button Remapping** — identical card layout to the old `remap.html`.

Both tabs operate on the same in-memory draft state and are saved together with one
**Save all changes to .ino** button, which downloads one backup and performs one file write containing
both sets of changes.

### 2.2 New v1.1.0 parameters exposed

The v1.1.0 firmware refactor (in `FirmwareUpdates/v1.1.0/`, not yet merged into `Firmware/`) added several
constants that were not present in v1.0.0 and were therefore invisible to the old `tuning.html`:

| New parameter | Section | Purpose |
|---|---|---|
| `CALIBRATION_MIN`, `CALIBRATION_MAX` | Smoothing & calibration | Startup sanity-check range for joystick center calibration; out-of-range triggers a 6-flash LED error. |
| `ROTATION_PRIORITY_THRESHOLD` | Axis behavior | Minimum combined rotation strength before rotation-priority can cancel translation. |
| `Z_PUSHPULL_THRESHOLD_MULT`, `Z_ROTATION_THRESHOLD_MULT`, `Z_ROTATION_DIVISOR` | Z-axis gesture detection (new section) | Named constants replacing what were magic numbers in v1.0.0's Z push/pull and twist detection. |
| `SLICER_MODE_HOLD_MS`, `SLICER_MODE_DEBOUNCE_MS` | Slicer mouse mode | Hold time and debounce for the CAD/slicer mode toggle combo (previously hardcoded). |
| `BUTTON_DEBOUNCE_MS` | Button debounce (new section) | Software debounce window for all physical button reads. |
| `LED_BLINK_ON_MS`, `LED_BLINK_OFF_MS`, `RX_LED_HOLD_MS` | LED feedback (new section) | Timing for the new TX/RX LED status feedback. |

All of these are additive — if a v1.0.0 `.ino` is opened (none of these constants exist yet), the
sliders fall back to firmware default values and simply won't find a matching constant to patch on save
(no error, no corruption).

### 2.3 Firmware-version awareness

The tool now parses the `Version:` comment in the `.ino` header and displays it as a badge next to the
file name (e.g. `firmware v1.1.0`).

It also sniffs for `orbit_logic.h` / `GAIN_TX_FP` references to detect the fixed-point firmware refactor
and shows a warning banner explaining that on that firmware:

- `GAIN_TX/TY/TZ/RX/RY/RZ`, `MAX_SPEED_SCALE`, and `RESPONSE_CURVE` are documentation-only constants in
  the `.ino` — the real math has moved to fixed-point equivalents in `orbit_logic.h` and is not editable
  by this tool.
- The **Speed mode profiles** section reads/writes `SPEED_MODE_SCALE[]` / `SPEED_MODE_RESPONSE_CURVE[]`,
  arrays which no longer exist in the v1.1.0 `.ino` (replaced by `SPEED_SCALE_FP[]` and lookup tables in
  `orbit_logic.h`). Edits to these controls are harmless but are silently discarded on save.

This prevents users from tuning sliders that look functional but have zero effect on-device — a
significant usability trap that existed implicitly in the original `tuning.html` (it didn't know about
`orbit_logic.h` at all, since v1.1.0 didn't exist yet when it was written).

### 2.4 Preserved behavior

- All regex-based parse/patch logic from both original tools was ported unchanged (only namespaced with
  `tn`/`rm` prefixes to avoid collisions in the merged script).
- The File System Access API flow (`showOpenFilePicker`, `createWritable`), backup-on-save
  (`makeBackup()` downloads a `.backup_<timestamp>` file before every save), and per-tab discard/reset
  behaviors are unchanged from the originals.
- Button remapping continues to support both the v1.0.0 single shared modifier constant
  (`SLICER_SHORTCUT_MODIFIER_PRIMARY`, not found → dropdown simply omitted for that slot) and v1.1.0's
  six independent per-slot modifier constants.

## 3. Verification performed

Because the File System Access API's file picker requires a real user gesture and cannot be driven by
automated browser tooling in this environment, verification was done by extracting the tool's pure
parse/patch functions into a Node.js `vm` sandbox and running them directly against the real firmware
files in the repository:

- `Firmware/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino` (v1.0.0, shipped)
- `FirmwareUpdates/v1.1.0/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino` (v1.1.0, unreleased)

Tests performed for both files:

1. Parse the full tuning state and print sample values.
2. Mutate several values (int, float, bool, speed-mode array) and apply the patch function.
3. Re-parse the patched text and confirm the mutated values round-trip correctly.
4. Parse the remap state (button actions + shortcut constants), mutate one constant, patch, and confirm
   round-trip.
5. Run the combined save-all sequence (tuning patch, then remap patch) and confirm both sets of changes
   are present in the final text.

Results:

- **v1.0.0**: all round-trips passed, including the speed-mode array patch (which is a real, functional
  constant on this firmware version).
- **v1.1.0**: all round-trips passed for every constant that still exists as a real firmware constant.
  The speed-mode-array round-trip is a documented no-op (see §2.3) — the regex patch does not match
  because the array no longer exists in this file, so the original text for that section is left
  unchanged. This confirmed no data corruption occurs; it simply confirms the fixed-point warning
  banner's claim in real firmware text.
- Loaded the combined HTML file directly in a browser to confirm zero console errors on page load.

Test harness scripts were temporary and removed from the repository after verification; the results are
recorded here for future reference.

## 4. Follow-up work

See the "Follow-ups" section of
[`CONFIGURATOR_MERGE_TASKLIST.md`](./CONFIGURATOR_MERGE_TASKLIST.md#follow-ups-not-done-in-this-pass) —
notably, real hardware/browser validation via manual file-picker interaction, and a possible future
enhancement to let the tool edit `orbit_logic.h` directly for v1.1.0+ firmware so gain/speed tuning is
fully functional again on that branch.
