# Calibration Wizard — Design Exploration

**Goal:** Every Orbit Controller build is slightly different (Hall sensor tolerances, magnet strength, soldering, joystick spring tension, wire routing). A guided calibration wizard would walk the user through a series of movements — left, right, forward, back, up, down, twist CW/CCW, tilt — and derive per-device settings automatically instead of hand-tuning constants.

---

## 1. What the firmware does today (and what's missing)

| Aspect | Current state | Gap |
|---|---|---|
| **Center calibration** | Startup-only: averages `CENTER_SAMPLES` (100) ADC readings on every power-up. User must not touch the knob for ~1s. | Not persisted. If the user bumps the knob during boot, it's mis-calibrated until replug. |
| **Range calibration** | None. `applyResponseCurve()` assumes a hardcoded full-scale of `1024 * GAIN` per axis. | Real sensors rarely span the full ADC range. A weak sensor axis feels sluggish; a strong one feels twitchy. Per-device min/max is unknown. |
| **Deadzone** | Fixed constants (`DEADZONE_INPUT = 40`, `DEADZONE_OUTPUT = 45`), tuned by editing the `.ino` and re-flashing. | Actual sensor noise varies per device. Deadzone should be derived from measured rest-noise. |
| **Axis direction / wiring check** | Manual `invX`…`invRZ` booleans in source. | Miswired or rotated joysticks require trial-and-error flashing. |
| **Z push/twist thresholds** | `DEADZONE_INPUT * 2` / `* 3` with 3-of-4 sensor agreement. | Threshold quality depends entirely on per-device sensor balance. |
| **Serial** | Debug telemetry only (`DEBUG_SERIAL`, compile-time off, one-way). | No command channel — the host can't ask the device anything. |
| **Config storage** | v1.1.0 already persists speed mode & slicer mode in **EEPROM**. | EEPROM infrastructure exists; calibration data could live there too. |
| **Existing tools** | `Configurator/*.html` edit constants in the `.ino` via the File System Access API, then the user re-flashes. No live device communication. | No runtime configuration path at all. |

**Key insight:** the ATmega32U4's USB CDC serial coexists with HID. A calibration channel does **not** require a second cable, a special bootloader, or breaking the SpaceMouse function — `Serial` can be open alongside the HID reports.

---

## 2. Architecture options

### Option A — Host-only wizard using existing debug output (quickest, zero/near-zero firmware change)
- Enable `DEBUG_SERIAL` (or make it default-on) so the firmware streams `v0..v7` centered raw values at 115200 baud.
- A host wizard (browser page with **WebSerial**, matching the existing HTML-tool ecosystem) reads the stream, guides the user through the movement steps, records min/max/noise per channel, and **computes recommended constants** (`DEADZONE_INPUT`, `DEADZONE_OUTPUT`, per-axis `GAIN_*`, `inv*` flags).
- Output feeds the existing `tuning.html` workflow: patch the `.ino`, re-flash once.

✅ No firmware protocol work; reuses tools users already have.
❌ Still requires one re-flash to apply results. Debug stream shows *centered* values (post `center[]` subtraction), so absolute ADC centers aren't visible — fine for range/noise, not for persisting centers.

### Option B — Serial calibration protocol + EEPROM (the "real" solution)
Add a tiny two-way serial command handler to the main firmware:

```
Host → Device                Device → Host
  I\n  (identify)              ORBIT v1.2.0
  R\n  (raw stream on/off)     R:512,498,530,505,511,502,499,520   (absolute ADC, ~50 Hz)
  C\n  (capture center now)    OK C:512,498,...
  W <blob>\n (write cal)       OK / ERR
  D\n  (dump cal)              CAL:{centers, mins, maxs, deadzone, inv flags}
  F\n  (factory reset cal)     OK
```

- Calibration data stored in EEPROM (versioned struct with a magic/checksum; the v1.1.0 EEPROM code is the template).
- At boot: if valid calibration exists in EEPROM, **skip or shorten** the startup center capture (or use stored centers as sanity bounds — fixes the "touched it during boot" problem).
- `applyResponseCurve()` gets its `inputMax` from stored per-axis range instead of the hardcoded `1024 * GAIN`.
- Host wizard talks WebSerial from a new `Configurator/calibrate.html` — same look/feel as the existing configurator, Chrome/Edge.

✅ No re-flash after calibration, ever. Calibrate → save → done. Survives replug.
❌ ~1–2 KB flash for the command parser + EEPROM struct (32U4 has ~28.6 KB usable; v1.1.0 headroom should be checked, but a line-based parser is cheap).

### Option C — Dedicated calibration firmware (separate sketch)
A standalone sketch that only streams raw ADC values and writes a calibration struct to EEPROM; the main firmware reads that struct at boot.

✅ Main firmware stays lean; calibration UX can be rich.
❌ Two flashes per calibration (cal firmware on, calibrate, main firmware back). EEPROM layout must be kept in lockstep across two sketches. Worst UX of the three — only worth it if Option B doesn't fit in flash.

### Recommendation
**Option B**, with **Option A as a stepping stone.** Build the browser wizard against the debug stream first (it validates the whole guided-movement/analysis flow with no firmware risk), then add the serial command handler + EEPROM struct so results persist on-device and re-flashing disappears entirely.

---

## 3. The wizard flow (host side)

A single-file `Configurator/calibrate.html` using WebSerial, structured as a step machine:

1. **Connect** — pick the serial port, identify firmware version.
2. **Rest / noise** *(3 s, hands off)* → per-channel center + noise stddev → recommended `DEADZONE_INPUT` = `k × max noise` (e.g., 4–5σ), and verifies startup-center sanity.
3. **Translate left → right** *(hold each extreme ~2 s)* → min/max for channels 1 & 5 → TX range; sign check → `invX`.
4. **Translate forward → back** → channels 3 & 7 → TY range, `invY`.
5. **Pull up → push down** → common-mode swing on channels 0/2/4/6 → TZ range, `invZ`, and validates the 3-of-4 push/pull detection threshold.
6. **Twist CW → CCW** → common-mode swing on channels 1/3/5/7 → RZ range, `invRZ`, twist threshold.
7. **Tilt left/right, forward/back** → RX/RY ranges from channels 0/4 and 2/6, `invRX`/`invRY`.
8. **Review screen** — live bar-graph per channel, computed values side-by-side with current firmware values, per-axis "weak sensor" warnings (e.g., a channel whose range is <60% of its sibling).
9. **Apply** — Option A path: export values / auto-patch `.ino` via File System Access API (reuse `tuning.html` code). Option B path: send `W <blob>` over serial → saved to EEPROM instantly.

Each step shows a simple diagram/arrow of the motion, a progress bar while sampling, and auto-advances when the reading is stable (no keyboard needed — hands stay on the device).

### What gets computed
| Output | Derived from |
|---|---|
| `center[8]` (persisted) | Rest-step averages |
| `DEADZONE_INPUT` | Rest-step noise (per-channel max, ×4–5) |
| Per-axis input range (replaces `1024 * GAIN` assumption) | Extreme-hold min/max per movement |
| Per-axis gain normalization | Ranges balanced so every axis reaches the same output ceiling |
| `invX/Y/Z/RX/RY/RZ` | Sign of observed deflection vs. expected direction |
| Z-push & twist thresholds | Measured common-mode magnitudes vs. per-channel ranges |
| Wiring sanity report | "Moved left but channel 3 responded" → detects swapped joystick connectors |

---

## 4. Firmware changes needed (Option B scope)

1. **`Serial.begin()` always** (CDC is free on the 32U4; only bytes actually sent cost anything). Poll `Serial.available()` once per loop — a few dozen bytes of code.
2. **Command parser** — single-letter commands, newline-terminated; ~100 lines in a new `orbit_serial.h`.
3. **EEPROM calibration struct** (extend the v1.1.0 persistence block):
   ```c
   struct OrbitCalibration {      // ~44 bytes
     uint16_t magic;              // 0x0CA1
     uint8_t  version;
     int16_t  center[8];
     int16_t  rangeNeg[8];        // measured min deflection per channel
     int16_t  rangePos[8];        // measured max deflection per channel
     uint8_t  deadzoneInput;
     uint8_t  invFlags;           // bit-packed invX..invRZ
     uint8_t  checksum;
   };
   ```
4. **Boot logic:** valid struct in EEPROM → use stored centers/ranges/deadzone/inversions; else fall back to current behavior (startup capture + compiled defaults). Nothing breaks for un-calibrated devices.
5. **`applyResponseCurve()` input:** use `rangePos/rangeNeg` per axis instead of `1024 * GAIN`.
6. **Raw stream mode:** on `R`, emit CSV of the 8 absolute ADC values at ~50 Hz; auto-disable after 60 s or on any command (so a stuck stream can't interfere with normal use).

Flash budget: parser + struct + stream ≈ 1.5–2 KB. Should be verified against the v1.1.0 build size before committing to Option B.

---

## 5. Why a browser wizard rather than a Python script

- The project already ships browser tools (`tuning.html`, configurator, remapper) — users have the muscle memory and no install step.
- WebSerial (Chrome/Edge) gives full two-way serial; a Python script would work identically but adds a Python + pyserial install burden.
- The same page can carry both back-ends: Option A (analyze debug stream → patch `.ino`) and Option B (serial protocol → EEPROM), auto-detected by the `I` identify command.
- A Python CLI could still be offered later as a power-user alternative; the analysis math is trivial to port.

---

## 6. Suggested phasing

- **Phase 1 (no firmware change):** `calibrate.html` wizard reading `DEBUG_SERIAL` output; results exported as constants / auto-patched into the `.ino`. Validates the movement-step UX and the analysis math.
- **Phase 2 (firmware v1.2.0):** serial command handler + EEPROM calibration struct; wizard writes directly to the device. Startup calibration becomes optional/fallback.
- **Phase 3 (polish):** wiring-fault detection, per-channel health report, "calibration age" indicator, factory-reset button in the wizard.