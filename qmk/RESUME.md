# QMK Port — Resume Point (August 9 2026)

## Current state

Branch: `feature/qmk-port`  
qmk_firmware branch: `hackman3d/multiaxis`  
qmk_firmware last commit: `bac6dd8` — `hackman3d: Phase 3 - analog reading + center calibration`

### Phases complete
| Phase | Status |
|---|---|
| 0 — Golden reference harness | ✅ `golden.csv` (838 rows), `reference_pipeline.c` |
| 1 — tmk_core multi-axis HID patch | ✅ descriptor bytes match Arduino verbatim; build 12,018 B |
| 2 — Minimal keyboard | ✅ flashes, enumerates as `256F:C631`, VIA shows device name |
| 3 — Analog + calibration | ✅ **hardware-verified** — see below. Committed as `bac6dd8` |

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

## Next up: Phase 4 — Axis pipeline

Port `orbit_logic.h` (the **fixed** v1.1.x header with `uint16_t` LUT tables — do not
reintroduce `uint8_t`) into `orbit_axes.c`:

- Gains, response curves (LUT), input deadzone
- Z push/pull consensus with negation (`transZ = -zPushPull`)
- Z twist consensus with `/2` (`Z_ROTATION_DIVISOR`)
- Rotation priority — **including the `smoothTX/TY/TZ` reset** (High risk #8)
- Triple output deadzone
- Axis inversion, defaults `RX/RY/RZ = true`
- Smoothing with the ±1 minimum step
- Keep `INPUT_MAX_RZ` at `1024 × gain` — the asymmetry vs TZ is correct (finding #4)

**Gate:** run the Phase 0 harness against the ported code and diff against
`qmk/test/golden.csv` — **the diff must be empty.** Also run `qmk/test/lut_fix_verify.c`
against the ported header — it must PASS.

See `qmk/06_TASKLIST.md` Phase 4 checklist.

## Then: Phase 5 — 6DOF output

- `orbit_6dof.c`: assemble and send Reports 1/2/3
- **The Y/Z swap: `send(oRX, oRZ, oRY, oTX, oTZ, oTY)`** — load-bearing, reproduce exactly
- Buttons → Report 3 bitmask; send unconditionally every scan
- Verify axis directions against the Arduino firmware in the 3Dconnexion panel; side-by-side
  in Fusion 360; measure report rate (~125 Hz target)

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