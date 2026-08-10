# QMK Port — Resume Point (August 9 2026)

## Current state

Branch: `feature/qmk-port`  
qmk_firmware branch: `hackman3d/multiaxis`  
Last qmk_firmware commit before Phase 6: `5fa63faa0d` — Phases 6 and 7 committed on top (see below)

### Phases complete
| Phase | Status |
|---|---|
| 0 — Golden reference harness | ✅ `golden.csv` (838 rows), `reference_pipeline.c` |
| 1 — tmk_core multi-axis HID patch | ✅ descriptor bytes match Arduino verbatim; build 12,018 B |
| 2 — Minimal keyboard | ✅ flashes, enumerates as `256F:C631`, VIA shows device name |
| 3 — Analog + calibration | ✅ **hardware-verified**. Committed as `bac6dd8` |
| 4 — Axis pipeline | ✅ **both gates passed** — see below. Committed as `5fa63faa0d` |
| 5 — 6DOF output | ✅ **verified in 3DxWare** — all 6 axes work; Fusion A/B + rate pending |
| 6 — Buttons, chords, LEDs | ✅ **COMPLETE** — host harness ALL PASS (25 checks) + all 5 tests confirmed on hardware |
| 7 — Slicer mouse mode | ✅ **COMPLETE** — host harness ALL PASS (36 checks) + hardware-verified in Bambu Studio |
| 8 — Live keymap editing (VIA) | ✅ **COMPLETE** — remap + persistence + dispatch all hardware-verified; double-fire fixed |
| 9 — Live tuning (VIA custom menus) | ✅ **code + host gates complete** — golden diff EMPTY, chord/slicer ALL PASS, all 3 keymaps build; hardware verification pending |

### Phase 4 gates (both passed, 2026-08-09)

The pipeline is header-only (`orbit_logic.h` verbatim + `orbit_pipeline.h` steps 1–14) in
the QMK keyboard folder, so the firmware and the host harness compile the *same* code:

- `qmk/test/qmk_pipeline.c` (host, MinGW gcc 15.2.0) → output diffed against
  `qmk/test/golden.csv`: **diff EMPTY** (0 bytes)
- `qmk/test/lut_fix_verify_qmk.c` against the ported `orbit_logic.h`: **ALL PASS**
  (uint16_t tables, 256 plateau intact, monotonic, low range unchanged)

`orbit_axes.c::orbit_axes_task()` runs ADC → pipeline → `orbit_axes` cache once per scan.
Speed mode (default 1) accessors `orbit_speed_mode()` / `orbit_set_speed_mode()` reset
smoothing on change, ready for Phase 6 chords.

### Phase 5 (hardware-verified in 3DxWare, 2026-08-09)

All 6 axes respond correctly in the 3Dconnexion view. Raw `v[]` rest values stay within
±14 counts after gestures (inside the input deadzone — normal Hall/mechanical hysteresis),
channel grouping correct under every gesture. Remaining: Fusion 360 A/B, report rate.

- `orbit_6dof.c` sends Reports 1/2/3 via the tmk_core fork's `send_multiaxis()` /
  `send_multiaxis_buttons()`, unconditionally every scan
- The load-bearing Y/Z swap lives at the call site in `orbit_controller.c`:
  `orbit_6dof_send(rx, rz, ry, tx, tz, ty)` — reproduces Arduino
  `sendCommand(oRX, oRZ, oRY, oTX, oTZ, oTY)` exactly (Trap 2)
- Button mask now comes from `orbit_hid_button_mask()` (Phase 6)
- Builds clean: `default` 14,868 B (51 %), `debug` 16,702 B (58 %)

### Phase 3 hardware verification (done)

Flashed the `debug` keymap (CONSOLE_ENABLE, `uprintf` raw dump every 500 ms) and verified
with `qmk console`:

- **At rest:** all 8 `v[]` values ≈ 0 — calibration passed, centers land in 300–750
- **Tilt gestures:** the expected channel pairs deviate (transX/transY axes)
- **Push straight down:** all **even** channels v[0,2,4,6] move the same direction (Z push/pull)
- **Twist CW/CCW:** all **odd** channels v[1,3,5,7] move the same direction (Z rotation)

The pin table `orbit_analog_pins[8] = {F6, F7, F4, F5, D7, D4, B5, B4}` is **confirmed
correct** — no reordering needed. Phase 3 exit criteria met.

Bootloader recovery is via the **physical RST switch** (double-tap RST to GND); all keymap
buttons are `KC_NO` since the 6DOF buttons will bypass the keymap entirely.

---

## Phase 6 — COMPLETE, hardware-confirmed (2026-08-09)

New/changed files in `keyboards/hackman3d/orbit_controller/`:

- **`orbit_chords.c`** (new) — verbatim port of `orbit_buttons.h` + the .ino's
  button section: `read_debounced_buttons()` (10 ms whole-mask stability),
  subset-match speed chord (all 3), **exact-match** slicer chord (buttons 1+2),
  `filterModeSwitchButtons` incl. emit-on-release, 250 ms chord window, 500 ms
  lockouts, 250 ms slicer hold-to-toggle. Exposes `orbit_hid_button_mask()`,
  `orbit_raw_button_mask()`, `orbit_chord_active()`. Reads pins D1/D0/E6
  directly (active LOW; direct matrix provides INPUT_PULLUP). EEPROM saves
  deferred to Phase 9.
- **`orbit_leds.c`** (new) — non-blocking TX blink mode+1 (80/120 ms), RX 500 ms
  hold on slicer enable, blocking 6-blink calibration error (boot only).
- **`orbit_slicer.c`** (new, Phase-6 STUB) — just the enabled flag +
  no-op `orbit_slicer_keys_task()` / `orbit_slicer_release_buttons()`;
  full slicer behaviour is Phase 7.
- **`orbit_controller.c`** — housekeeping now runs `orbit_chords_task()` →
  `orbit_axes_task()` → 6DOF sends with `orbit_hid_button_mask()` →
  `orbit_leds_task()`. post_init uses `orbit_leds_init()` +
  `orbit_leds_signal_speed_mode()`.
- **`orbit_axes.c`** — calibration error blink now calls
  `orbit_leds_flash_calibration_error()`.
- **`keymaps/debug/keymap.c`** — also prints `mode/slicer/btn/hid/chord` every
  500 ms for hardware chord verification.

Builds clean: `default` 14,868 B (51 %), `debug` 16,702 B (58 %).

### Host verification (gate passed)

`qmk/test/chord_test.c` + `qmk/test/mock/orbit_controller.h` compile the *real*
`orbit_chords.c` on the host with a simulated clock/buttons — **ALL PASS**
(25 checks), covering all five tasklist chord tests incl. both negative:

```powershell
# from qmk/test (uses QMK MSYS gcc):
S:\QMK_MSYS\usr\bin\bash.exe -lc "cd /d/GitHub/rossw42/HackMan3D-Orbit-Controller/qmk/test && cp /d/GitHub2/qmk_firmware/keyboards/hackman3d/orbit_controller/orbit_chords.c mock/ && gcc -Wall -Wextra -O2 -I mock -o chord_test.exe chord_test.c mock/orbit_chords.c && ./chord_test.exe"
```

### Hardware confirmation (all 5 passed via `qmk console`, debug keymap)

1. ✅ Each button alone → `btn:1/2/4 hid:1/2/4` after the chord window
2. ✅ All 3 held → `btn:7 hid:0 chord:1`, mode cycled 1→2→0→1, zero HID leak
3. ✅ Buttons 1+2 held → `btn:6 hid:0 chord:1`, `slicer:0→1`, RX LED
4. ✅ All 3 held → `slicer:` value never changed (mutual exclusion)
5. ✅ Quick tap → emit-on-release blips visible (`btn:1 hid:0` → `btn:1 hid:1`)

Note: slicer state is RAM-only until Phase 9 — it resets to off on replug.

---

## Phase 7 — Slicer mouse mode — COMPLETE, hardware-confirmed (2026-08-09)

Changed files in `keyboards/hackman3d/orbit_controller/`:

- **`orbit_slicer.c`** (stub → full implementation) — verbatim port of the
  .ino slicer section (lines 385–531):
  - `pointing_device_driver_init()` returns **true** (the weak default
    returns false, which puts pointing_device in INIT_FAILED and no report
    is ever generated — trap avoided).
  - `pointing_device_driver_get_report()` == `sendSlicerMouse()`: reads the
    `orbit_axes` cache (never re-runs the pipeline), `ry+rz` → mouse X /
    `rx` → mouse Y (rotation), `tx/ty` (translation), zoom exclusivity
    (|tz| ≥ 90 → drops drag button, wheel only, early return), auto-drag
    (left button held while panning, released when idle).
  - `scale_mouse_axis()` / `scale_mouse_wheel()` verbatim: deadzone 45,
    divisor 120, clamp ±12; wheel threshold 90, full scale 700, repeat
    interval 125→45 ms, **inverted sign** (tz > 0 scrolls −1, High risk #22).
  - `orbit_slicer_keys_task()` == `updateSlicerMouseButtons()`: 650 ms
    short/long press dispatch via `keymap_key_to_keycode(_SLICER /
    _SLICER_LONG)` + `tap_code16_delay(kc, 20)` — pulled forward from the
    Phase 8 plan (the keymap layers already existed; with VIA in Phase 8
    they become live-remappable with zero further slicer changes). Chord
    active (`orbit_chord_active()`) suppresses/cancels pending presses.
  - The `_SLICER`/`_SLICER_LONG` layers are pure lookup tables — never
    `layer_on()`'d (that would make the direct matrix fire keycodes
    itself, bypassing the 650 ms dispatch).
- **`orbit_controller.c`** — housekeeping now has the real slicer branch
  (== .ino:711-720): slicer mode zeroes the 6DOF axes + buttons
  (`orbit_6dof_send(0,…)` + `orbit_6dof_send_buttons(0)`) and runs
  `orbit_slicer_keys_task()`; 6DOF mode calls
  `orbit_slicer_release_buttons()` every scan (drops a held drag on mode
  exit).
- `POINTING_DEVICE_DRIVER = custom` / `pointing_device: true` /
  `POINTING_DEVICE_TASK_THROTTLE_MS 1` were already in place since Phase 2.

Builds clean: `default` 16,028 B (55 %), `debug` 17,792 B (62 %).

### Host verification (gate passed)

`qmk/test/slicer_test.c` + extended `qmk/test/mock/orbit_controller.h`
compile the *real* `orbit_slicer.c` on the host — **ALL PASS** (36 checks):
disabled passthrough, deadzone idle, translation/rotation pan mapping +
min-step + clamp, rotation-vs-translation priority, zoom exclusivity, wheel
inverted sign + rate-limit (interval scaling, direction flip, latch reset),
short/long press dispatch, chord suppression, drag release on mode exit.

```powershell
# from qmk/test (QMK MSYS MinGW64 gcc):
S:\QMK_MSYS\usr\bin\env.exe MSYSTEM=MINGW64 CHERE_INVOKE=1 S:\QMK_MSYS\usr\bin\bash.exe -lc "cd /d/GitHub/rossw42/HackMan3D-Orbit-Controller/qmk/test && cp /d/GitHub2/qmk_firmware/keyboards/hackman3d/orbit_controller/orbit_slicer.c mock/ && gcc -Wall -Wextra -O2 -I mock -o slicer_test.exe slicer_test.c mock/orbit_slicer.c && ./slicer_test.exe"
```

`chord_test.c` re-run after the mock header extension: still **ALL PASS**.

### Hardware verification (PASSED — Bambu Studio, 2026-08-09)

Tested with the `debug` keymap in Bambu Studio — everything works: drag-pan,
inverted wheel zoom, button shortcuts, and chords. `qmk console` output
confirmed `slicer:1` with `hid:0` throughout (6DOF buttons suppressed in
slicer mode), live axis data driving the mouse, and chord detection intact
(`btn:7 hid:0 chord:1` with mode cycling 2→3→0 and zero HID/shortcut leaks).

**Phase 7 exit criteria MET.**

## Phase 8 — Live keymap editing (VIA) — COMPLETE, hardware-verified (2026-08-10)

`keymaps/viam/` (existing since Phase 2) verified against the complete
Phase 5–7 codebase; no code changes were needed:

- `keymaps/viam/rules.mk` — `VIA_ENABLE = yes` (`orbit_via.c` stays commented
  out until Phase 9)
- `keymaps/viam/config.h` — `DYNAMIC_KEYMAP_LAYER_COUNT 3`,
  `DYNAMIC_KEYMAP_MACRO_COUNT 0` (macros would claim ~800 B of the 1 KB
  EEPROM)
- `keymaps/viam/keymap.c` — `_BASE` all `KC_NO` (6DOF buttons bypass the
  keymap), `_SLICER` = Tab / N / Ctrl+0, `_SLICER_LONG` = Shift+Alt+G / L / A
  — verified against the .ino constant tables (mod `0x06`+key `0x0A` →
  `LSA(KC_G)`; mod `0x01`+key `0x27` → `LCTL(KC_0)`)

Remap path (zero slicer changes, by design): `orbit_slicer.c::
run_slicer_button_action()` → weak `keymap_key_to_keycode()` → with VIA,
`dynamic_keymap.c`'s strong `keycode_at_keymap_location()` reads the
EEPROM-backed dynamic keymap. So VIA edits to layers 1/2 flow straight into
the 650 ms short/long dispatch.

**Build passes:** `viam` = **15,866 B (55 %, 12,806 B free)**; endpoint check
OK (RAW HID uses the shared endpoint — no conflict with the multi-axis EP).

**Bug found on hardware (2026-08-09) — FIXED:** in slicer mode, a physical
press fired the layer-0 keycode through normal QMK processing *and* the
650 ms slicer dispatch (double-fire). Layer 0 had only been inert by
accident (all `KC_NO`); once VIA let the user remap it, the direct matrix
started firing. Fix in `orbit_controller.c`: `process_record_kb()` returns
`false` unconditionally — the matrix never fires keycodes; all button
behaviour is owned by `orbit_chords.c` + `orbit_slicer.c`. VIA is unaffected
(raw_hid path), and slicer dispatch is unaffected (`keymap_key_to_keycode()`
+ `tap_code16_delay()` bypass process_record). Layer 0 is now a true
lookup-only layer; the size drop from 17,388 B is LTO stripping the
unreachable action paths.

New sidecar for testing: `qmk/via/hackman3d_orbit_controller_phase8.json` —
keymap-only (name/vendorProductId/matrix/layout). Do **not** load the full
`hackman3d_orbit_controller.json` yet; its 5 custom-menu tabs need
`orbit_via.c` (Phase 9) and would render dead.

### Hardware results (2026-08-09/10 — all passed):

1. ✅ VIA connects with the *phase8* JSON (after flashing `viam` — the first
   attempt had `debug` flashed, which has no `VIA_ENABLE`/RAW HID)
2. ✅ Remap in VIA takes effect without reflash
3. ✅ Remap survives replug
4. ✅ Double-fire fix retested via live `qmk console` trace: in slicer mode
   all 3 buttons dispatch the correct layer-1 defaults (`kc=0x002B` Tab /
   `0x0011` N / `0x0127` Ctrl+0); in 6DOF mode buttons are joystick buttons
   only, **no keypresses — by design, matches the Arduino firmware**. A
   `slicer dispatch: btn=N long=X kc=0x....` trace (CONSOLE_ENABLE only,
   compiled out of `viam`) was added to `run_slicer_button_action()` in
   `orbit_slicer.c`; `debug` build = 16,184 B (56 %)

**Support note:** every "keypresses don't work" report during testing traced
to slicer mode not being engaged — the toggle is the **btn:6 pair held
250 ms** (console prints `slicer:1`, RX LED lights), and it resets to off on
every reflash/replug (RAM-only until Phase 9). The `btn:3` pair is *not* the
combo and correctly leaks to HID as plain joystick buttons.

**Phase 8 exit criteria MET.**

---

## Phase 9 — Live tuning (VIA custom menus) — code + host gates COMPLETE (2026-08-10)

Every tunable now lives in a single EEPROM-backed struct (`g_config`) read
live each scan; VIA custom menus write it over raw HID. Hardware
verification (VIA GUI end-to-end + persistence) is the remaining step.

New/changed in `keyboards/hackman3d/orbit_controller/`:

- **`orbit_config.h`** (new) — pure-C `orbit_config_t` (packed, **62 B**,
  fits `EECONFIG_KB_DATA_SIZE 64`): deadzones, smoothing divisor,
  6 gains (fp/256), invert mask, speed mode/scales/curves, rotation
  priority, Z-detection multipliers, flags (dominant-axis / auto-drag /
  slicer-active / suppress-chord), all slicer tunables, all
  chord/debounce/calibration timings. `ORBIT_CONFIG_DEFAULTS` = Arduino
  v1.1.0 values verbatim, single source of truth shared with the host
  harnesses. `ORBIT_CONFIG_MAGIC 0xB1` — bump on any layout change.
- **`orbit_config.c`** (new) — `eeconfig` datablock load/save,
  `orbit_config_validate()` clamps EVERY field (a zero divisor from a raw
  HID packet would otherwise brick the scan loop until reflash), magic
  mismatch → defaults. Slicer enable and speed mode now **persist across
  replug** (was RAM-only since Phase 6).
- **`orbit_via.c`** (new, viam keymap only) — `via_custom_value_command_kb`:
  5 channels (0=Axes 1=Speed 2=Filters 3=Slicer 4=System), value IDs match
  `qmk/via/hackman3d_orbit_controller.json` exactly (verified 1:1), range
  controls are big-endian u16, toggles/dropdowns 1 byte. Every set_value
  runs `orbit_config_validate()`; EEPROM written only on `id_custom_save`.
  System channel includes two momentary actions: **Recalibrate now** and
  **Reset all to defaults**. Speed-mode and slicer-active setters route
  through the same functions the chords use (smoothing reset + LED signal
  + persist).
- **`orbit_pipeline.h` / `orbit_logic.h`** — compile-time constants replaced
  with `g_config` reads (gains, deadzones, speed scaling, rotation priority,
  Z-detection, invert mask, dominant-axis flag). Curves stay as the 3
  PROGMEM LUTs; `speed_curve_idx[]` selects per mode.
- **`orbit_chords.c` / `orbit_slicer.c` / `orbit_axes.c`** — timings,
  slicer tunables and calibration bounds read from `g_config`;
  chord-member suppression now behind `ORBIT_FLAG_SUPPRESS_CHORD`.
- **`keymaps/viam/rules.mk`** — `SRC += orbit_via.c` enabled.

### Host gates (all passed, 2026-08-10; local gcc 15.2.0 via scoop)

- **Golden gate re-run (the Phase 9 gate):** `qmk_pipeline.c` defines
  `orbit_config_t g_config = ORBIT_CONFIG_DEFAULTS;` — all-default config
  through the live-config pipeline diffs **EMPTY** against `golden.csv`.
- **`chord_test.exe` ALL PASS** (g_config instance added to the harness).
- **`slicer_test.exe` ALL PASS — 36 checks** (g_config +
  `orbit_config_save()` no-op stub, since `orbit_slicer_set_enabled()` now
  persists).
- Harness builds now also need `orbit_config.h` copied into `mock/`.

### Builds (all clean, 2026-08-10)

| Keymap | Size | Free |
|---|---|---|
| `default` | 16,674 B (58 %) | 11,998 B |
| `debug` | 18,410 B (64 %) | 10,262 B |
| `viam` | 19,526 B (68 %) | 9,146 B |

Phase 9 cost ≈ 3.7 KB on `viam` (config + validate + VIA handler) — still
comfortably inside the 05_SIZE_BUDGET envelope.

### Remaining for Phase 9 exit (hardware)

1. Flash `viam`, sideload the **full** `qmk/via/hackman3d_orbit_controller.json`
   (the 5 custom-menu tabs are now live — the phase8 JSON is obsolete)
2. Move a slider (e.g. TZ gain) → axis response changes live, no reflash
3. Save in VIA → replug → values persist (incl. slicer mode + speed mode)
4. "Reset all to defaults" restores golden behaviour
5. "Recalibrate now" with stick centred → centers re-captured

(Phase 5 leftovers folded into Phase 10 validation: Fusion 360 A/B side-by-side,
report-rate measurement ~125 Hz.)

---

## Key file locations

| What | Where |
|---|---|
| Flash script | `D:\GitHub\rossw42\HackMan3D-Orbit-Controller\qmk\test\flash.ps1` |
| Golden reference CSV | `D:\GitHub\rossw42\HackMan3D-Orbit-Controller\qmk\test\golden.csv` |
| Fixed orbit_logic.h | `D:\GitHub\rossw42\HackMan3D-Orbit-Controller\FirmwareUpdates\v1.1.0\...\orbit_logic.h` |
| Task list | `D:\GitHub\rossw42\HackMan3D-Orbit-Controller\qmk\06_TASKLIST.md` |
| QMK keyboard folder | `D:\GitHub2\qmk_firmware\keyboards\hackman3d\orbit_controller\` |
| QMK firmware branch | `D:\GitHub2\qmk_firmware` on `hackman3d/multiaxis` |
| Docs branch | `D:\GitHub\rossw42\HackMan3D-Orbit-Controller` on `feature/qmk-port` |

## Flashing (Caterina)

```powershell
# 1. Start flash.ps1 FIRST (it will wait):
cd D:\GitHub\rossw42\HackMan3D-Orbit-Controller\qmk\test
.\flash.ps1 -Keymap debug -TimeoutS 30

# 2. While it's waiting — double-tap the RST switch (RST to GND twice within ~500 ms)
```

Caterina's bootloader window is ~8 s after a double-tap.

---

## Lessons learned (don't repeat)

1. **VIRTSER and JOYSTICK_MULTIAXIS_ENABLE are mutually exclusive** on the 32U4 (7 endpoints
   needed, 6 available). Bootloader entry is via the physical RST switch.
2. **`dprintf()` requires `debug_config.enable = true`** at runtime. Use `uprintf()` in the
   debug keymap to be safe.
3. All keymap buttons are `KC_NO` — the 6DOF buttons (Phase 5/6) bypass the keymap. Recovery
   depends entirely on the physical RST switch, which is now installed and verified working.