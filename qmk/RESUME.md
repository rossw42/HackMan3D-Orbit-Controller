# QMK Port — Resume Point (August 9 2026)

## Current state

Branch: `feature/qmk-port`  
qmk_firmware branch: `hackman3d/multiaxis`  
qmk_firmware last commit: `5fa63faa0d` — `hackman3d/orbit_controller: Phase 4+5 - axis pipeline and 6DOF output`

### Phases complete
| Phase | Status |
|---|---|
| 0 — Golden reference harness | ✅ `golden.csv` (838 rows), `reference_pipeline.c` |
| 1 — tmk_core multi-axis HID patch | ✅ descriptor bytes match Arduino verbatim; build 12,018 B |
| 2 — Minimal keyboard | ✅ flashes, enumerates as `256F:C631`, VIA shows device name |
| 3 — Analog + calibration | ✅ **hardware-verified**. Committed as `bac6dd8` |
| 4 — Axis pipeline | ✅ **both gates passed** — see below. Committed as `5fa63faa0d` |
| 5 — 6DOF output | 🔶 **code complete** — hardware verification pending |

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

### Phase 5 code (committed, not yet hardware-verified)

- `orbit_6dof.c` sends Reports 1/2/3 via the tmk_core fork's `send_multiaxis()` /
  `send_multiaxis_buttons()`, unconditionally every scan
- The load-bearing Y/Z swap lives at the call site in `orbit_controller.c`:
  `orbit_6dof_send(rx, rz, ry, tx, tz, ty)` — reproduces Arduino
  `sendCommand(oRX, oRZ, oRY, oTX, oTZ, oTY)` exactly (Trap 2)
- Button mask hardwired to `0` until Phase 6 lands `orbit_hid_button_mask()`
- Builds clean: `default` 13,278 B (46 %), `debug` 15,018 B (52 %)

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

## Next up: Phase 5 hardware verification

Flash `hackman3d_orbit_controller_default.hex` (already built) and:

1. Verify all 6 axes in the 3Dconnexion control panel move in the **same direction** as the
   Arduino firmware
2. Side-by-side test in Fusion 360: orbit, pan, zoom
3. Measure report rate (~125 Hz target)

If an axis direction is wrong, suspect the inversion defaults or the Y/Z swap call site in
`orbit_controller.c` — do NOT touch the pipeline (it is golden-verified).

## Then: Phase 6 — Buttons, chords, LEDs

- Port `orbit_buttons.h` → `orbit_chords.c` verbatim (debounce, chord detect,
  speed-mode cycle → `orbit_set_speed_mode()`, slicer toggle)
- Wire `orbit_hid_button_mask()` into the `orbit_6dof_send_buttons()` call (currently 0)
- `orbit_leds.c` — TX blink mode+1 non-blocking, RX 500 ms hold
- See `qmk/06_TASKLIST.md` Phase 6 checklist (five chord tests incl. two negative)

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