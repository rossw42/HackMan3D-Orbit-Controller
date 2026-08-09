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
| 7 — Slicer mouse mode | 🟡 **host gate PASSED** (slicer_test.c, 36 checks) — hardware slicer test pending |

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

## Phase 7 — Slicer mouse mode (host gate PASSED, 2026-08-09)

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

### Hardware verification (REMAINING — do this next)

Flash `default` (or `debug` for console) and test in a slicer (Cura/Prusa):

1. Toggle slicer mode (buttons 1+2 held 250 ms, RX LED) → 6DOF stops, mouse starts
2. Tilt → left-drag pan; twist/rotate → left-drag with `ry+rz`→X / `rx`→Y
3. Push/pull (tz) → wheel zoom, direction matches the Arduino build (inverted
   sign is intentional), repeat speeds up with deflection
4. Zoom while panning → drag released, wheel only (exclusivity)
5. Short-press each button → Tab / N / Ctrl+0; hold ≥ 650 ms → Shift+Alt+G / L / A
6. Chords still work in slicer mode and never leak shortcuts
7. Toggle back → mouse stops (drag released), 6DOF resumes

## Next up: after Phase 7 hardware test → Phase 8 — Live keymap editing (VIA)

`keymaps/viam/` with `VIA_ENABLE = yes`, `DYNAMIC_KEYMAP_LAYER_COUNT 3`,
`DYNAMIC_KEYMAP_MACRO_COUNT 0`. The slicer dispatch already goes through
`keymap_key_to_keycode()`, so VIA remaps take effect with no further slicer
changes. See `qmk/06_TASKLIST.md` Phase 8.

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