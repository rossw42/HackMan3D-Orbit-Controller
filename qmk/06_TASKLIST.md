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

## Phase 1 — QMK core fork (multi-axis HID) ✅ COMPLETE

- [x] Create branch `hackman3d/multiaxis` in `d:\GitHub2\qmk_firmware`
- [x] Add `JOYSTICK_MULTIAXIS_ENABLE` gate
- [x] `tmk_core/protocol/usb_descriptor.c` — multi-axis report descriptor
- [x] `tmk_core/protocol/report.h` — 3 packed report structs
- [x] `tmk_core/protocol/lufa/lufa.c` — `send_multiaxis()` + `send_multiaxis_buttons()`
- [x] Endpoint count passes (joystick uses SHARED_IN_EPNUM; VIRTSER incompatible — disabled)
- [x] **Byte-diff PASSED** — ELF bytes `05 01 09 08 a1 01 ...` match `hidReportDescriptor[]`
- [x] Patch exported to `qmk/patches/0001-multiaxis-hid.patch`

**Exit criteria MET:** descriptor hex is byte-identical to the Arduino firmware's (12,018 B).

**Note:** VIRTSER (1200-baud-touch auto-reset) is disabled in this build. The multi-axis
descriptor requires a dedicated joystick endpoint, which fills the last free EP on the
ATmega32U4 (6 EPs total). Use button 3 (`QK_BOOT`) to enter the bootloader. See `rules.mk`.

---

## Phase 2 — Minimal QMK keyboard that enumerates ✅ COMPLETE

- [x] `keyboards/hackman3d/orbit_controller/keyboard.json` (doc `02` §3)
- [x] `config.h`, `rules.mk`, `readme.md`
- [x] `keymaps/default/keymap.c` — 3 buttons, 3 layers
- [x] `keymaps/viam/` with `VIA_ENABLE`, 3 layers, 0 macros
- [x] `orbit_controller.h` — pin table, layer enum, module API
- [x] `orbit_controller.c` — post_init / housekeeping skeleton
- [x] **Both builds succeed:** `default` = 10,574 B (36 %), `via` = 11,920 B (41 %)
- [x] Flash to hardware
- [x] Device enumerates as `0x256F:0xC631`, product string "SpaceMouse Pro Wireless (cabled)"
- [x] 3DxWare recognises it (axes will read 0 — that's expected at this stage)
- [x] Confirm we can still flash back to the Arduino firmware (Caterina round-trip)

**Exit criteria:** 3DxWare sees the device. This de-risks the whole project — if the
descriptor doesn't bind, stop and fix before writing any math.

---

## Phase 3 — Analog reading + calibration ✅ COMPLETE

- [x] `ANALOG_DRIVER_REQUIRED = yes`
- [x] `orbit_analog_pins[8] = {F6, F7, F4, F5, D7, D4, B5, B4}` (doc `02` §2)
- [x] `keyboard_post_init_kb()`: LED init, 800 ms wait, `orbit_calibrate_center()`
- [x] Calibration sanity check + error flash
- [x] Temporary `CONSOLE_ENABLE` debug keymap printing all 8 raw values
- [x] **Verify the channel grouping using the 4-joystick matrix geometry:**
      The four JH16 joysticks are mechanically coupled under the puck — you cannot move them
      independently. Instead verify by gesture:
      - At rest: all 8 `v[]` values ≈ 0 (calibration passed, centers in 300–750)
      - Tilt forward/back: channels 5,7 or 1,3 deviate (transX/transY axes)
      - Push straight down: all even channels v[0,2,4,6] move same direction (Z push/pull)
      - Twist clockwise/CCW: all odd channels v[1,3,5,7] move same direction (Z rotation)
      If the grouping is wrong (e.g. Z push shows on odd channels), swap the pin order in
      `orbit_analog_pins[]`. Do NOT swap pairs — the pairs must match the joystick geometry.
- [x] Confirm centers land in 300–750

**Exit criteria MET (verified on hardware):** all 8 channels read plausible values and the
index mapping is confirmed against physical movement — at rest all 8 `v[]` ≈ 0 (centers in
300–750), tilt gestures deviate the expected channel pairs, Z push/pull moves all **even**
channels together and Z twist moves all **odd** channels together. The pin table
`{F6, F7, F4, F5, D7, D4, B5, B4}` is confirmed correct — no reordering needed.
Committed to `hackman3d/multiaxis` as `bac6dd8` (`hackman3d: Phase 3 - analog reading +
center calibration`).

---

## Phase 4 — Axis pipeline ✅ COMPLETE

- [x] Port `orbit_logic.h` → `orbit_logic.h` (kept header-only, verbatim) — carried over the
      **already-applied** curve LUT fix (`uint16_t` tables); `uint8_t` not reintroduced
- [x] Keep `INPUT_MAX_RZ` at `1024 × gain` — the asymmetry vs TZ is correct (finding #4)
- [x] Port the pipeline (steps 1–14) — lives in `orbit_pipeline.h` (header-only, so the
      firmware and the host harness compile the *same* code), driven from
      `orbit_axes.c::orbit_axes_task()`
- [x] Rotation priority — **including the `smoothTX/TY/TZ` reset** (High risk #8)
- [x] Z push/pull consensus with negation (`transZ = -zPushPull`)
- [x] Z twist consensus with `/2`
- [x] Triple output deadzone
- [x] Axis inversion, defaults `RX/RY/RZ = true`
- [x] Smoothing with the ±1 minimum step
- [x] **Phase 0 harness vs ported code — diff EMPTY.** `qmk/test/qmk_pipeline.c` compiles
      the ported headers on the host; output diffed against `qmk/test/golden.csv`: 0 bytes.
- [x] `qmk/test/lut_fix_verify_qmk.c` against the ported `orbit_logic.h` — **ALL PASS**

**Exit criteria MET:** golden CSV diff clean (verified 2026-08-09, MinGW gcc 15.2.0).
Committed to `hackman3d/multiaxis` as `5fa63faa0d`.

**Design note:** the plan said `orbit_logic.c/.h`; the port is header-only
(`orbit_logic.h` + `orbit_pipeline.h`) so the identical translation units are compiled by
both the AVR firmware and the host harness — the golden diff proves the firmware math
itself, not a copy of it.

---

## Phase 5 — 6DOF output (hardware-verified in 3DxWare; Fusion A/B + rate pending)

- [x] `orbit_6dof.c`: assemble and send Reports 1/2/3 (via the tmk_core fork's
      `send_multiaxis()` / `send_multiaxis_buttons()`)
- [x] **The Y/Z swap: `send(oRX, oRZ, oRY, oTX, oTZ, oTY)`** (Inventory Trap 2, High risk
      #15) — performed at the call site in `orbit_controller.c` housekeeping, commented
- [x] Buttons → Report 3 bitmask (mask hardwired to 0 until Phase 6 lands
      `orbit_hid_button_mask()`)
- [x] Send unconditionally every scan (matches Arduino)
- [x] Verify all 6 axes in the 3Dconnexion control panel move in the **same direction** as the
      Arduino firmware — **VERIFIED on hardware 2026-08-09**: all 6 axes respond correctly in
      the 3Dconnexion view; raw `v[]` rest values stay within ±14 counts (inside the input
      deadzone) with correct channel grouping under every gesture
- [ ] Side-by-side test in Fusion 360: orbit, pan, zoom
- [ ] Measure report rate (~125 Hz target)

Builds clean: `default` 13,278 B (46 %), `debug` 15,018 B (52 %). Commit `5fa63faa0d`.

**Exit criteria:** a user cannot distinguish it from the Arduino firmware in Fusion 360.

---

## Phase 6 — Buttons, chords, LEDs

- [x] Port `orbit_buttons.h` → `orbit_chords.c` **verbatim** (High risk #20)
- [x] `readDebouncedButtons` with `timer_read32()`
- [x] `isModeSwitchComboPressed` — subset match
- [x] `isSlicerModeComboPressed` — **exact** match (High risk #19)
- [x] `filterModeSwitchButtons` incl. the emit-on-release path
- [x] `filterSlicerModeButtons`
- [x] `updateSpeedMode` — rising edge, 500 ms lockout, `resetSmoothing()`
- [x] `updateSlicerMouseMode` — 250 ms hold-to-toggle, 500 ms lockout
- [x] `orbit_leds.c` — TX blink `mode+1` non-blocking, RX 500 ms hold
- [x] Test: tap each button → registers as joystick button (host + **hardware** ✅)
- [x] Test: all 3 → speed mode cycles, LED blinks 1/2/3, no spurious button presses (host + **hardware** ✅)
- [x] Test: buttons 1+2 held 250 ms → slicer toggles, RX LED (host + **hardware** ✅)
- [x] Test: pressing all 3 does **not** trigger the slicer toggle (mutual exclusion) (host + **hardware** ✅)
- [x] Test: press-and-release a chord member alone → button still emitted (emit-on-release) (host + **hardware** ✅)

**Exit criteria MET (2026-08-09):** all five chord tests pass, including the two negative
tests — first in the host harness (`qmk/test/chord_test.c` compiles the *real*
`orbit_chords.c` against `qmk/test/mock/orbit_controller.h`: **ALL PASS**, 25 checks),
then confirmed on hardware via `qmk console` with the `debug` keymap:
`btn:1/2/4 → hid:1/2/4` per-button; all-3 chord `btn:7 hid:0 chord:1` with mode cycling
1→2→0→1 and no HID leak; slicer combo `btn:6 hid:0 chord:1` toggled `slicer:0→1`;
`slicer` unchanged during all-3 holds; quick taps show emit-on-release transitions.

---

## Phase 7 — Slicer mouse mode

- [x] `POINTING_DEVICE_DRIVER = custom`, implement `pointing_device_driver_get_report()`
- [x] Read from the axis cache — **do not re-run the pipeline** (doc `02` §6)
- [x] `scaleMouseAxis` / `scaleMouseWheel` ported
- [x] Wheel rate-limited repeat with **inverted sign** (High risk #22) (host ✅)
- [x] Zoom exclusivity (returns early) (host ✅)
- [x] Auto-drag hold/release (host ✅)
- [x] `ry+rz` → mouse X, `rx` → mouse Y (host ✅)
- [x] Zero the 6DOF axes + buttons while in slicer mode
- [x] Short/long-press shortcuts via `keymap_key_to_keycode()` + `tap_code16_delay()`
      (650 ms timer, chord suppression) — pulled forward from the Phase 8 plan
      since the stub had to become real anyway (host ✅)
- [x] Host gate: `qmk/test/slicer_test.c` compiles the *real* `orbit_slicer.c` —
      **ALL PASS** (36 checks, 2026-08-09)
- [x] Test in a slicer: pan, rotate, zoom all behave as before — **hardware-verified
      in Bambu Studio (2026-08-09)**: drag-pan, inverted wheel zoom, shortcuts and
      chords all work; console shows `slicer:1 hid:0` (6DOF buttons suppressed) and
      chord detection (`btn:7 chord:1`, mode cycling) intact in slicer mode

**Exit criteria MET:** slicer mode behaves identically to the Arduino firmware.

---

## Phase 8 — Live keymap editing (VIA)

- [ ] `keymaps/viam/` with `VIA_ENABLE = yes`
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

## Status of the five findings

Of the five, **one was a real bug (now fixed), one was a false positive, three need no
action.** The only file changed in the Arduino firmware is
`FirmwareUpdates/v1.1.0/.../orbit_logic.h`.

Findings 2 and 3, and the "two traps" at the top of `01_FIRMWARE_INVENTORY.md`, are **not
defects to remove** — they are load-bearing behaviours or harmless documentation mismatches.

| # | Finding | Severity | Status |
|---|---|---|---|
| 1 | **Curve LUT truncation** — 8 entries written as `256` stored as `0` in a `uint8_t` array; output collapsed to zero past ~88 % deflection. | 🔴 Real bug | ✅ **FIXED** in `orbit_logic.h` (approved). See below. |
| 2 | Float `GAIN_*` / `MAX_SPEED_SCALE` / `RESPONSE_CURVE` are dead code | 🟡 Confusing, harmless | **No action.** Left in place in v1.1.x; simply not ported. The QMK version has one source of truth (the config struct in VIA), so the duplication cannot recur. |
| 3 | `SLICER_MODE_BUTTONS = {1,2,0}` but `COUNT = 2`, so the `0` is ignored | 🟡 Confusing, harmless | **No action.** Port as an explicit 2-button chord with a comment. Behaviour identical. |
| 4 | `INPUT_MAX_RZ` uses `1024` while `INPUT_MAX_TZ` uses `2048`, though both sum 4 channels | 🟢 **FALSE POSITIVE** | ❌ **Do not change** — see below. Tested before editing; the constants are all correct. |
| 5 | `ledFlashCalibrationError()` blocks 720 ms | 🟢 Not a bug | **No action.** Runs in `post_init`, before reports start. |

### Finding #4 — retracted: all six INPUT_MAX constants are correct

This was approved for a fix, but testing it first showed the fix would have been a
**regression**. `qmk/test/input_max_check.c` derives each axis's true post-gain range from the
pipeline and compares against the declared constant:

```
axis  formula            pre-gain  gain   true max   declared   verdict
TX    v5 - v1                1024   333       1332       1332   OK
TY    v7 - v3                1024   333       1332       1332   OK
TZ    -(v0+v2+v4+v6)         2048   589       4712       4712   OK
RX    v4 - v0                1024   461       1844       1844   OK
RY    v2 - v6                1024   461       1844       1844   OK
RZ    (v1+v3+v5+v7)/2        1024   512       2048       2048   OK
```

The asymmetry is correct because **`rotZ` is divided by `Z_ROTATION_DIVISOR` (=2)** at
`.ino:652`, which halves the four-channel sum back to a two-channel-equivalent range.
`transZ` has no such divisor, so it correctly uses 2048.

Had we "fixed" `INPUT_MAX_RZ` to `2048 * 512`, `applyResponseCurve()` would normalise against
4096 while the real maximum is 2048 — so RZ could never exceed 50 % normalised input, and
`pow(0.5, 1.6) ≈ 0.33` means **Z-rotation would have lost roughly two thirds of its output
range.** The only change needed is the comment at `orbit_logic.h:282`, which says
"analogRange = 1024 for X/Y, 2048 for Z" without mentioning the RZ divisor.

### The Y/Z swap is NOT a bug

`sendCommand(oRX, oRZ, oRY, oTX, oTZ, oTY)` looks like a mistake but is the 3Dconnexion
coordinate convention (Y-up on the wire vs Z-up internally). **Reproduce it exactly.** It is
documented as a "trap" only because a careful porter would otherwise be tempted to "fix" it.

### Bug #1 — APPLIED to `orbit_logic.h`

**What changed** (4 edits, no table data touched):

1. `CURVE_TABLE_1_9`, `CURVE_TABLE_1_6`, `CURVE_TABLE_1_3`: `uint8_t` → **`uint16_t`**
2. `lookupCurve()` parameter: `const uint8_t*` → `const uint16_t*`
3. `applyResponseCurve()` local `tbl`: `const uint8_t*` → `const uint16_t*`
4. Header comment corrected: divisor is `i/56`, not `i/63`; explains the plateau and why the
   type must be `uint16_t`

All 192 table entries are **byte-for-byte unchanged**. Cost: +192 bytes flash (three 64-entry
tables at 2 bytes instead of 1). RAM unaffected.

**Why not regenerate from `pow(i/63, curve)`** — my original suggestion, which was wrong. The
data fits **`i/56`** (mean abs error 2.81 vs 17.25). Entry 56 is the first `256`, so full
output at **88.9 % deflection** with a flat top ~11 % is deliberate: you don't have to bottom
out the stick. Regenerating with `i/63` would move saturation to 100 % and make the device
feel *slower*.

**Verified** by `qmk/test/lut_fix_verify.c`, which compiles against the live header:

```
[PASS] CURVE_TABLE_1_6[56..63] all store 256 (not 0)
[PASS] sizeof entry is 2 bytes (uint16_t)
[PASS] output is monotonically non-decreasing
[PASS] full deflection returns 256
[PASS] norm8 0..219 unchanged by the fix

norm8   old (broken)   new (fixed)   change
  216            247           247       +0
  224              0           256     +256
  248              0           256     +256
```

The change is **surgical**: identical below norm8 220, restores full scale above it. Also
confirmed: `g++ -Wall` now emits **zero** `-Woverflow` warnings (it previously emitted 8).

**Verified on the real target too** (`qmk/test/avr_target_check.sh`) — the host tests prove the
math but not that the code is valid for AVR:

```
toolchain: avr-g++ (qmk/qmk_toolchains) 15.2.0
OK: compiles clean for atmega32u4 (-Wall -Wextra -Werror=overflow)
three CURVE_TABLE_* arrays: 384 bytes   ->  the fix costs exactly +192 bytes
```

The +192 figure is therefore measured, not arithmetic on paper.

#### Expected behaviour change on hardware

Only the **top ~14 % of deflection** on any axis is affected. Previously the axis went *dead*
there (output fell to the deadzone); now it holds full scale. Users of v1.1.0 will notice the
extremes feel *stronger* — that is the intended v1.1.0 design finally working.

#### Rollback

Per your request, this is recorded so it can be reverted if hardware testing disappoints:

The fix is commit **`d3ee98f`** — `fix(firmware): curve LUT truncation`. It touches only
`orbit_logic.h`, nothing else.

```bash
# Inspect it:
git show d3ee98f

# Revert just the firmware fix, keeping all docs and tests:
git revert d3ee98f
```

The fix is one self-contained commit touching only `orbit_logic.h`, so reverting is clean.
`qmk/test/lut_fix_verify.c` will then fail — which is the correct signal that the bug is back.

**Test on hardware before shipping:** push each axis to its mechanical limit in Fusion 360 and
confirm motion is smooth and maximal rather than cutting out. Compare against a v1.1.0 build.

---

## Progress

| Phase | Status |
|---|---|
| 0 — Golden reference harness | ✅ **COMPLETE** — `reference_pipeline.c` + `golden.csv` committed |
| 1 — QMK core fork | ✅ **COMPLETE** — patch committed, descriptor bytes verified, build 12,018 B |
| 2 — Minimal keyboard | ✅ **COMPLETE** — flashes, enumerates, VIA shows the name |
| 3 — Analog + calibration | ✅ **COMPLETE** — hardware-verified: channel grouping correct, centers in 300–750; commit `bac6dd8` |
| 4 — Axis pipeline | ✅ **COMPLETE** — golden CSV diff EMPTY, lut_fix_verify_qmk ALL PASS; commit `5fa63faa0d` |
| 5 — 6DOF output | ✅ **Verified in 3DxWare** — all 6 axes work in the 3Dconnexion view; remaining: Fusion 360 A/B + report-rate measurement |
| 6 — Buttons/chords/LEDs | ✅ **COMPLETE** — host harness ALL PASS (25 checks) + all 5 tests confirmed on hardware via qmk console |
| 7 — Slicer mouse | ✅ **COMPLETE** — host harness ALL PASS (36 checks) + hardware-verified in Bambu Studio |
| 8 — Live keymap (VIA) | ☐ Not started |
| 9 — Live tuning (VIA menus) | ☐ Not started |
| 10 — Validation & release | ☐ Not started |
