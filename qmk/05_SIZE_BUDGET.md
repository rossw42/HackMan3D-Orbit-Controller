# 05 — Size Budget (Measured)

Target: ATmega32U4, Caterina bootloader → **28,672 bytes** usable flash, **2,560 bytes** RAM,
**1,024 bytes** EEPROM.

All flash numbers below are **real `avr-gcc` builds**, not estimates. Method:

```bash
# QMK MSYS at S:\QMK_MSYS, repo at d:\GitHub2\qmk_firmware
make handwired/onekey/promicro:<keymap> <FEATURE_VARS> LTO_ENABLE=yes
```

`handwired/onekey/promicro` is an atmega32u4 + Caterina board, so QMK reports `MAX_SIZE`
as exactly **28672** — identical to our target.

---

## 1. Measured — the actual keyboard

These are builds of `keyboards/hackman3d/orbit_controller` as scaffolded (Phase 2: pin table
+ `post_init`/`housekeeping` skeleton, joystick + pointing device + all trims + LTO):

| Build | Flash | % | Free |
|---|---|---|---|
| `hackman3d/orbit_controller:default` | **10,574** | 36 % | **18,098** |
| `hackman3d/orbit_controller:via` | **11,920** | 41 % | **16,752** |

**VIA costs only 1,346 bytes** on this keyboard. That is the headline result: the original
feasibility study's "+4–6 KB, likely does not fit" was off by roughly 4×.

`.hex` sizes on disk are 29,767 / 33,551 bytes — that's Intel HEX ASCII encoding, not flash
usage. The binary figures above are what matter.

### Cross-check on a proxy board

Measured earlier against `handwired/onekey/promicro` (also atmega32u4 + Caterina, so the same
28,672-byte ceiling), to isolate individual feature costs:

| Config | Flash | % | Free |
|---|---|---|---|
| Joystick + LTO | 11,370 | 39 % | 17,302 |
| Joystick + Pointing device (custom) + LTO | 12,108 | 42 % | 16,564 |
| Joystick + Pointing device + VIA + LTO | 14,172 | 49 % | 14,500 |

Pointing device with a custom driver costs only **738 bytes**. The proxy board's totals run
higher than our real keyboard because it does not apply our trims.



### Headroom for our code

The Orbit Controller's own logic is small and, critically, **integer-only**:

| Component | Estimate |
|---|---|
| `orbit_logic.c` (gains, curve lookup, smoothing, deadzones) | ~600 B |
| 3 curve LUTs as `uint16_t` (64 × 2 × 3) | 384 B |
| `orbit_axes.c` pipeline | ~1,200 B |
| `orbit_chords.c` debounce + chord state machine | ~800 B |
| `orbit_slicer.c` mouse + wheel repeat | ~900 B |
| `orbit_6dof.c` report assembly | ~300 B |
| `orbit_leds.c` | ~300 B |
| `orbit_config.c` defaults + validate | ~500 B |
| `orbit_via.c` 50-value get/set switch | ~1,500 B |
| **Total application code** | **~6,500 B** |

The `orbit_via.c` estimate is the least certain — a 50-case switch on AVR is mostly a jump
table plus per-case loads. The reference project `ploopy_viamenus` handles **~90 values** on
the same MCU, so 50 is comfortably within proven territory.

### Why the original feasibility study was pessimistic

`FirmwareTuning/QMK_PORT_FEASIBILITY.md` estimated 22–25 KB for the core port and concluded
VIA "likely does not fit." Two things changed:

1. It assumed a **float `pow()` response curve** costing ~2 KB. v1.1.0 already replaced that
   with a fixed-point LUT — that cost is gone, and no float math remains in the hot path
   (except one `ROTATION_PRIORITY` constant folded at compile time).
2. Its QMK baseline of "18–23 KB" was too high. Measured baseline is **11.4 KB**, because we
   disable `extrakey`, `mousekey`, `nkro`, `magic`, `command`, `bootmagic`, `space_cadet` and
   `grave_esc` — none of which a 3-button 6DOF controller needs.

---

## 2. Projected shipping build

| Component | Flash | Source |
|---|---|---|
| `orbit_controller:via` skeleton | 11,920 | **measured** |
| Orbit Controller application code | ~6,500 | estimated |
| **Projected total** | **~18,400** | |
| **Budget** | **28,672** | |
| **Projected free** | **~10,300 (36 %)** | |

**Verdict: it fits comfortably.** The QMK-side cost is measured on the real keyboard; only our
own ~6.5 KB of application code is an estimate. Even if the application came in at **2.5× the
estimate** (16 KB) we would still fit.

Re-measure at Phase 9 (complete) to confirm.



---

## 3. EEPROM budget — the real constraint

1,024 bytes total on the 32U4, and VIA's dynamic keymaps live here.

QMK/VIA layout:

| Region | Size |
|---|---|
| QMK `eeconfig` core (magic, debug, keymap config, layers, backlight, audio, RGB…) | 34 B |
| `EECONFIG_KB_DATA_SIZE` (our tuning struct) | **64 B** |
| VIA layout options | 2 B |
| Dynamic keymap: `layers × rows × cols × 2` = 3 × 1 × 3 × 2 | **18 B** |
| Dynamic encoders (none) | 0 B |
| Dynamic macros: `DYNAMIC_KEYMAP_MACRO_COUNT 0` | **0 B** |
| **Total** | **~118 B of 1,024** |

**~906 bytes free.** EEPROM is a non-issue *because* the matrix is only 1×3.

### The one EEPROM trap

`DYNAMIC_KEYMAP_MACRO_COUNT` defaults to 16, and QMK sizes the macro buffer as "all remaining
EEPROM." Left at the default it would claim ~800 bytes for macros we will never use. Setting
it to `0` is what keeps this comfortable:

```c
#define DYNAMIC_KEYMAP_MACRO_COUNT 0
```

If macros are wanted later there's room, but they're meaningless for a 3-button device.

### Struct sizing

The tuning struct (doc `04` §4) is ~61 bytes; we allocate 64. If it needs to grow, up to
~128 bytes is affordable. Include a `version` byte so a layout change triggers a clean
defaults reset rather than loading garbage.

---

## 4. Trim list (if flash gets tight)

In order of value-for-effort:

| Trim | Saving | Cost |
|---|---|---|
| `LTO_ENABLE = yes` | 1.5–3 KB | none — **already assumed in all numbers above** |
| `CONSOLE_ENABLE = no` in shipping builds | 1.5–2 KB | no `qmk console` debugging |
| `MAGIC_ENABLE = no` | ~500 B | no magic keycodes (unneeded) |
| `COMMAND_ENABLE = no` | ~1 KB | no command mode (unneeded) |
| `EXTRAKEY_ENABLE = no` | ~500 B | no media/system keys |
| `MOUSEKEY_ENABLE = no` | ~1.5 KB | none — we use `pointing_device`, not mouse *keys* |
| `NKRO_ENABLE = no` | ~500 B | irrelevant for 3 buttons |
| `SPACE_CADET_ENABLE = no`, `GRAVE_ESC_ENABLE = no` | ~300 B | unneeded |
| `DYNAMIC_KEYMAP_LAYER_COUNT` 3 → 2 | ~50 B EEPROM | lose the long-press layer |
| Merge the 3 curve LUTs into 1 + a shaping multiplier | ~256 B | changes feel — **last resort** |
| Drop VIA, use a bespoke Raw HID protocol | ~3–4 KB | lose GUI keymap editing; would need our own WebHID tool |

The last two should not be needed. Everything above `DYNAMIC_KEYMAP_LAYER_COUNT` is already
in the planned `rules.mk`/`keyboard.json`.

---

## 5. RAM budget

2,560 bytes. Not a concern:

| Component | RAM |
|---|---|
| QMK base (USB buffers, matrix, keymap cache, timers) | ~1,000–1,200 B |
| `orbit_config_t` (RAM working copy) | 61 B |
| `center[8]` (int16) | 16 B |
| Smoothing accumulators + axis cache | ~30 B |
| Chord state machine | ~30 B |
| Slicer per-button state (3 × timers/flags) | ~30 B |
| VIA raw HID buffer | 32 B |
| **Total** | **~1,250–1,450 B** |

~1,100 bytes of stack headroom. The pipeline uses no recursion, no dynamic allocation, and no
large locals (the biggest is `int16_t raw[8]`).

One caution: the calibration routine in the Arduino version uses `long sum[8]` = 32 bytes of
stack. Trivial, but keep it as a local in `post_init` rather than a global.

---

## 6. CPU budget

| Item | Cost |
|---|---|
| 8 × `analogReadPin()` @ ~112 µs | ~900 µs |
| Axis pipeline (integer only, LUT lookup) | ~50 µs |
| Chord logic | ~10 µs |
| 2–3 USB endpoint writes | ~30 µs |
| QMK matrix scan + housekeeping | ~100 µs |
| **Per scan** | **~1.1 ms** |

Target output rate is the SpaceMouse-typical **125 Hz (8 ms)**, so we use ~14 % of the
budget. Plenty of margin — and none of it goes to software floating point, because the
fixed-point conversion in v1.1.0 already eliminated that.

If the free-running scan rate turns out to be *faster* than the host wants, throttle the
6DOF send to 8 ms rather than slowing the matrix scan (which would add button latency).

---

## 7. Summary

| Resource | Used (projected) | Limit | Verdict |
|---|---|---|---|
| Flash | ~18.4 KB (11,920 measured + ~6.5 KB app) | 28,672 B | ✅ fits, ~36 % free |
| RAM | ~1.4 KB | 2,560 B | ✅ comfortable |
| EEPROM | ~118 B | 1,024 B | ✅ trivial |
| USB endpoints | 4 | 6 | ✅ comfortable |
| CPU | ~14 % @ 125 Hz | — | ✅ large margin |

**The ATmega32U4 is sufficient for the full-featured port including VIA live keymap editing
and live tuning of all 50 parameters.** No MCU swap is required — the RP2040 discussion in the
original feasibility study (§8) is moot.

The binding constraint was never the hardware; it was the assumption that float math and
QMK's default feature set had to come along. Neither does.
