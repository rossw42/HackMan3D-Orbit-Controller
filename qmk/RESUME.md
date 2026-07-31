# QMK Port — Resume Point (July 31 2026)

## Current state

Branch: `feature/qmk-port` — 22 commits  
qmk_firmware branch: `hackman3d/multiaxis`  
Last commit: `233ff11` (docs/task list)

### Phases complete
| Phase | Status |
|---|---|
| 0 — Golden reference harness | ✅ `golden.csv` (838 rows), `reference_pipeline.c` |
| 1 — tmk_core multi-axis HID patch | ✅ descriptor bytes match Arduino verbatim; build 12,018 B |
| 2 — Minimal keyboard | ✅ flashes, enumerates as `256F:C631`, VIA shows device name |
| 3 — Analog + calibration | ✅ `orbit_axes.c` written; calibration active; debug keymap ready |

### What's on the device right now

**Currently flashed:** `hackman3d/orbit_controller:debug` (old build with all buttons = `KC_NO`)  
**Problem:** Can't reach the bootloader — VIRTSER disabled, all buttons KC_NO, RST pins inaccessible

---

## What to do when you open the device

### Step 1 — Flash the recovery hex (one time, to restore boot access)

A new hex is already built and waiting:
```
D:\GitHub2\qmk_firmware\hackman3d_orbit_controller_debug.hex
```

This hex has:
- **Button 1 (leftmost) = QK_BOOT** — reliable way back to the bootloader
- `uprintf()` — always outputs to `qmk console`  
- `debug_config.enable = true`

To flash it:
```powershell
# 1. Start flash.ps1 FIRST (it will wait 30s):
cd D:\GitHub\rossw42\HackMan3D-Orbit-Controller\qmk\test
.\flash.ps1 -Keymap debug -TimeoutS 30

# 2. While it's waiting — double-tap RST pin to GND on the Pro Micro
```

**RST pin location on Pro Micro:** 2nd pin from the USB connector end, bottom header row, adjacent to a GND pin. Double-tap = two brief contacts within ~500 ms. This opens an 8-second bootloader window.

### Step 2 — Verify Phase 3 (raw ADC values)

After flashing:
```bash
qmk console
```

Expected output (every 500 ms):
```
Ψ Console Connected: 3Dconnexion SpaceMouse Pro Wireless (cabled) (256F:C631:1)
raw: 3 -1 0 2 -5 4 -2 1
```

**What to verify** (the sensors are in a 4-joystick matrix — you can't move them independently):
- **At rest:** all 8 values ≈ 0 (calibration passed)
- **Tilt the puck:** two specific channels deviate
- **Push straight down:** all **even** channels v[0,2,4,6] move in the **same direction**
- **Twist:** all **odd** channels v[1,3,5,7] move in the **same direction**

If the even/odd grouping is **wrong** (e.g., pushing down moves odd channels), update `orbit_analog_pins[]` in:
```
D:\GitHub2\qmk_firmware\keyboards\hackman3d\orbit_controller\orbit_controller.h
```

### Step 3 — Phase 4: Axis pipeline

Once channel grouping is confirmed, start Phase 4:
- Port `orbit_logic.h` → `orbit_logic.c` (already fixed: `uint16_t` LUT tables)
- Implement the full pipeline in `orbit_axes.c` (gains, Z consensus, rotation priority, smoothing)
- Diff output against `qmk/test/golden.csv` — must be empty

See `qmk/06_TASKLIST.md` Phase 4 checklist.

---

## Key file locations

| What | Where |
|---|---|
| Recovery debug hex | `D:\GitHub2\qmk_firmware\hackman3d_orbit_controller_debug.hex` |
| Flash script | `D:\GitHub\rossw42\HackMan3D-Orbit-Controller\qmk\test\flash.ps1` |
| Golden reference CSV | `D:\GitHub\rossw42\HackMan3D-Orbit-Controller\qmk\test\golden.csv` |
| Task list | `D:\GitHub\rossw42\HackMan3D-Orbit-Controller\qmk\06_TASKLIST.md` |
| QMK keyboard folder | `D:\GitHub2\qmk_firmware\keyboards\hackman3d\orbit_controller\` |
| QMK firmware branch | `D:\GitHub2\qmk_firmware` on `hackman3d/multiaxis` |
| Docs branch | `D:\GitHub\rossw42\HackMan3D-Orbit-Controller` on `feature/qmk-port` |

---

## Lessons learned (don't repeat)

1. **Always have QK_BOOT on at least one button** in any keymap where VIRTSER is disabled. The debug keymap now has button 1 = QK_BOOT permanently.
2. **VIRTSER and JOYSTICK_MULTIAXIS_ENABLE are mutually exclusive** on the 32U4 (7 endpoints needed, 6 available). The debug keymap doesn't have the multiaxis descriptor anyway, so VIRTSER *could* be re-enabled for the debug build — consider this for next time.
3. **`dprintf()` requires `debug_config.enable = true`** at runtime. Use `uprintf()` in the debug keymap to be safe.
