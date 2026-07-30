# 06 — Task List

Phased plan. Each phase ends in something testable. Do not start a phase before its
predecessor's exit criteria are met.

---

## Phase 0 — Golden reference harness ⭐

**Why first:** this port's biggest risk is a silent numerical regression in the axis
pipeline. Five behaviors were flagged High risk in the Inventory. The only way to be sure the
QMK math matches the Arduino math is to run both on a PC and diff the outputs. Building this
harness first makes every later phase verifiable instead of hopeful.

- [ ] Create `qmk/test/` with a plain-`gcc` harness
- [ ] Compile the **original** `orbit_logic.h` (it has no Arduino deps by design) into
      `reference.exe`
- [ ] Generate a test vector set: sweep each of the 8 channels across 0–1023, plus combined
      cases that trigger Z push/pull consensus, Z twist consensus, and rotation priority
- [ ] Dump `(inputs) → (oTX..oRZ)` to CSV for all 3 speed modes
- [ ] Extract the axis pipeline (`.ino:626-708`) into a standalone `reference_pipeline.c` so
      the *whole* pipeline is covered, not just the leaf math functions
- [ ] Commit the CSV as the golden reference

**Exit criteria:** `reference.exe` reproducible, golden CSV committed.

---

## Phase 1 — QMK core fork (multi-axis HID)

- [ ] Create branch `hackman3d/multiaxis` in `d:\GitHub2\qmk_firmware`
- [ ] Add `JOYSTICK_MULTIAXIS_ENABLE` gate
- [ ] `tmk_core/protocol/usb_descriptor.c` — multi-axis report descriptor (doc `03` §3)
- [ ] `tmk_core/protocol/report.h` — the 3 packed report structs
- [ ] `tmk_core/protocol/lufa/lufa.c` — `send_multiaxis()` + `send_multiaxis_buttons()`
- [ ] Verify endpoint count still passes the compile-time `#error` check
- [ ] **Byte-diff the generated descriptor against the Arduino `hidReportDescriptor[]`**
- [ ] Export the patch to `qmk/patches/0001-multiaxis-hid.patch`

**Exit criteria:** descriptor hex is byte-identical to the Arduino firmware's.

---

## Phase 2 — Minimal QMK keyboard that enumerates

- [x] `keyboards/hackman3d/orbit_controller/keyboard.json` (doc `02` §3)
- [x] `config.h`, `rules.mk`, `readme.md`
- [x] `keymaps/default/keymap.c` — 3 buttons, 3 layers
- [x] `keymaps/via/` with `VIA_ENABLE`, 3 layers, 0 macros
- [x] `orbit_controller.h` — pin table, layer enum, module API
- [x] `orbit_controller.c` — post_init / housekeeping skeleton
- [x] **Both builds succeed:** `default` = 10,574 B (36 %), `via` = 11,920 B (41 %)
- [ ] Flash to hardware
- [ ] Device enumerates as `0x256F:0xC631`, product string "SpaceMouse Pro Wireless (cabled)"
- [ ] 3DxWare recognises it (axes will read 0 — that's expected at this stage)
- [ ] Confirm we can still flash back to the Arduino firmware (Caterina round-trip)

**Exit criteria:** 3DxWare sees the device. This de-risks the whole project — if the
descriptor doesn't bind, stop and fix before writing any math.

---

## Phase 3 — Analog reading + calibration

- [ ] `ANALOG_DRIVER_REQUIRED = yes`
- [ ] `orbit_analog_pins[8] = {F6, F7, F4, F5, D7, D4, B5, B4}` (doc `02` §2)
- [ ] `keyboard_post_init_kb()`: LED init, 800 ms wait, `orbit_calibrate_center()`
- [ ] Calibration sanity check + error flash
- [ ] Temporary `CONSOLE_ENABLE` debug keymap printing all 8 raw values
- [ ] **Verify each of the 8 channels maps to the correct physical sensor** — press each
      joystick individually and confirm the expected `v[]` index moves
- [ ] Confirm centers land in 300–750

**Exit criteria:** all 8 channels read plausible values and the index mapping is confirmed
against physical movement. Do not trust the pin table without this check.

---

## Phase 4 — Axis pipeline

- [ ] Port `orbit_logic.h` → `orbit_logic.c/.h`
- [ ] **Fix the curve LUT `256 → 0` truncation bug** (Inventory §1.4): store as `uint16_t`
- [ ] Port the pipeline (steps 1–14) into `orbit_axes.c`
- [ ] Rotation priority — **including the `smoothTX/TY/TZ` reset** (High risk #8)
- [ ] Z push/pull consensus with negation (`transZ = -zPushPull`)
- [ ] Z twist consensus with `/2`
- [ ] Triple output deadzone
- [ ] Axis inversion, defaults `RX/RY/RZ = true`
- [ ] Smoothing with the ±1 minimum step
- [ ] **Run the Phase 0 harness against the ported code — diff must be empty** (except for
      the intentional LUT bug fix, which must be characterised and explained)

**Exit criteria:** golden CSV diff clean. This is the gate that proves no feature was lost.

---

## Phase 5 — 6DOF output

- [ ] `orbit_6dof.c`: assemble and send Reports 1/2/3
- [ ] **The Y/Z swap: `send(oRX, oRZ, oRY, oTX, oTZ, oTY)`** (Inventory Trap 2, High risk #15)
- [ ] Buttons → Report 3 bitmask
- [ ] Send unconditionally every scan (matches Arduino)
- [ ] Verify all 6 axes in the 3Dconnexion control panel move in the **same direction** as the
      Arduino firmware
- [ ] Side-by-side test in Fusion 360: orbit, pan, zoom
- [ ] Measure report rate (~125 Hz target)

**Exit criteria:** a user cannot distinguish it from the Arduino firmware in Fusion 360.

---

## Phase 6 — Buttons, chords, LEDs

- [ ] Port `orbit_buttons.h` → `orbit_chords.c` **verbatim** (High risk #20)
- [ ] `readDebouncedButtons` with `timer_read32()`
- [ ] `isModeSwitchComboPressed` — subset match
- [ ] `isSlicerModeComboPressed` — **exact** match (High risk #19)
- [ ] `filterModeSwitchButtons` incl. the emit-on-release path
- [ ] `filterSlicerModeButtons`
- [ ] `updateSpeedMode` — rising edge, 500 ms lockout, `resetSmoothing()`
- [ ] `updateSlicerMouseMode` — 250 ms hold-to-toggle, 500 ms lockout
- [ ] `orbit_leds.c` — TX blink `mode+1` non-blocking, RX 500 ms hold
- [ ] Test: tap each button → registers as joystick button
- [ ] Test: all 3 → speed mode cycles, LED blinks 1/2/3, no spurious button presses
- [ ] Test: buttons 1+2 held 250 ms → slicer toggles, RX LED
- [ ] Test: pressing all 3 does **not** trigger the slicer toggle (mutual exclusion)
- [ ] Test: press-and-release a chord member alone → button still emitted (emit-on-release)

**Exit criteria:** all five chord tests pass, including the two negative tests.

---

## Phase 7 — Slicer mouse mode

- [ ] `POINTING_DEVICE_DRIVER = custom`, implement `pointing_device_driver_get_report()`
- [ ] Read from the axis cache — **do not re-run the pipeline** (doc `02` §6)
- [ ] `scaleMouseAxis` / `scaleMouseWheel` ported
- [ ] Wheel rate-limited repeat with **inverted sign** (High risk #22)
- [ ] Zoom exclusivity (returns early)
- [ ] Auto-drag hold/release
- [ ] `ry+rz` → mouse X, `rx` → mouse Y
- [ ] Zero the 6DOF axes + buttons while in slicer mode
- [ ] Test in a slicer: pan, rotate, zoom all behave as before

**Exit criteria:** slicer mode behaves identically to the Arduino firmware.

---

## Phase 8 — Live keymap editing (VIA)

- [ ] `keymaps/via/` with `VIA_ENABLE = yes`
- [ ] `DYNAMIC_KEYMAP_LAYER_COUNT 3`, `DYNAMIC_KEYMAP_MACRO_COUNT 0`
- [ ] 3-layer keymap: base (joystick buttons), slicer-short, slicer-long
- [ ] `orbit_slicer.c` long-press dispatch via `keymap_key_to_keycode()` + `tap_code16()`
- [ ] Confirm the 6 default shortcuts match the Arduino defaults exactly
      (Tab / Shift+Alt+G / N / L / Ctrl+0 / A)
- [ ] Test: remap a button in VIA → takes effect without reflash
- [ ] Test: remap persists across replug

**Exit criteria:** button actions editable live; defaults unchanged from Arduino.

---

## Phase 9 — Live tuning (VIA custom menus)

- [ ] `orbit_config.h/.c` — the config struct + defaults + `magic` + `orbit_config_validate()`
- [ ] `EECONFIG_KB_DATA_SIZE 64`
- [ ] `orbit_config_load()` / `save()` / `eeconfig_init_kb()`
- [ ] Replace every hardcoded constant in the pipeline with a `g_config` read
- [ ] Recompute `INPUT_MAX` from the live gains (doc `04` §4) — preserving the TZ=2048 /
      RZ=1024 asymmetry
- [ ] `orbit_via.c` — `via_custom_value_command_kb()` with all 5 channels, 50 values
- [ ] **Big-endian** u16 packing
- [ ] **Clamp/validate every setter** — a zero divisor bricks the device until reflash
- [ ] Recalibrate + reset-to-defaults actions
- [ ] `qmk/via/hackman3d_orbit_controller.json`
- [ ] Load in VIA Design tab; verify all 5 tabs render
- [ ] Test: drag each slider → immediate effect on device behavior
- [ ] Test: save → survives replug
- [ ] Test: reset to defaults → matches Arduino defaults exactly
- [ ] Test: try to set smooth divisor to 0 → clamped, device survives
- [ ] Record final firmware size

**Exit criteria:** all 50 values editable live and persisted; no value can hang the device.

---

## Phase 10 — Validation & release

- [ ] Full behavior checklist from Inventory §7 — all 30 rows verified on hardware
- [ ] Host matrix from doc `03` §7 (Windows/macOS/Linux, 3DxWare/spacenavd, Fusion/Blender)
- [ ] Side-by-side A/B against the Arduino firmware with a real user
- [ ] Confirm final size has headroom for future features
- [ ] `CONSOLE_ENABLE` off in the shipping build
- [ ] Document the flashing procedure (Caterina, and how to get back to Arduino)
- [ ] Update `CHANGELOG.md`
- [ ] Update `qmk/README.md` with actual measured numbers
- [ ] Decide on the WebHID configurator follow-up (doc `04` §8 option 3)

---

## Known bugs to fix during the port

| # | Bug | Where | Action |
|---|---|---|---|
| 1 | Curve LUT entries written as `256` truncate to `0` in a `uint8_t` array, collapsing the response curve at high deflection | `orbit_logic.h:51-84` | Store as `uint16_t`. Characterise the behavior change and confirm with the user that the *fixed* curve is desired. |
| 2 | Float gain constants are dead code, misleading anyone tuning the firmware | `.ino:58-66` | Removed by design — VIA exposes the real FP values. |
| 3 | `SLICER_MODE_BUTTONS` declares 3 entries but `SLICER_MODE_BUTTON_COUNT = 2`, so the third is silently ignored | `.ino:165-166` | Make the chord explicit; document it. |
| 4 | `INPUT_MAX_RZ` uses `1024` while `INPUT_MAX_TZ` uses `2048`, though both sum 4 channels | `orbit_logic.h:284-289` | **Preserve as-is** — changing it changes feel. Flag for the user as a possible intentional choice. |
| 5 | `ledFlashCalibrationError()` blocks for 720 ms | `.ino:270-275` | Acceptable in `post_init`; keep. |

Bug 1 and Bug 4 need a **decision from the user** before Phase 4 completes, since both change
the device's feel.

---

## Progress

| Phase | Status |
|---|---|
| 0 — Golden reference harness | ☐ Not started |
| 1 — QMK core fork | ☐ Not started |
| 2 — Minimal keyboard | ◧ Scaffolded & building; hardware test pending |
| 3 — Analog + calibration | ☐ Not started |
| 4 — Axis pipeline | ☐ Not started |
| 5 — 6DOF output | ☐ Not started |
| 6 — Buttons/chords/LEDs | ☐ Not started |
| 7 — Slicer mouse | ☐ Not started |
| 8 — Live keymap (VIA) | ☐ Not started |
| 9 — Live tuning (VIA menus) | ☐ Not started |
| 10 — Validation & release | ☐ Not started |
