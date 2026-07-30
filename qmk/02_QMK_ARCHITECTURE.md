# 02 — QMK Architecture

Target: `d:\GitHub2\qmk_firmware\keyboards\hackman3d\orbit_controller\`

QMK checkout: **0.33.8+3** (`2dc5e397`), branch `master`.

**Status:** scaffolded and building — `default` = 10,574 B, `via` = 11,920 B. Files marked
*(planned)* below do not exist yet; they land in Phases 3–9 of `06_TASKLIST.md`.

---

## 1. File layout

```
keyboards/hackman3d/
└── orbit_controller/
    ├── keyboard.json            # data-driven config: MCU, pins, matrix, USB IDs, features
    ├── config.h                 # things keyboard.json can't express
    ├── rules.mk                 # ANALOG_DRIVER_REQUIRED, POINTING_DEVICE_DRIVER
    ├── readme.md                # QMK-facing readme
    ├── orbit_controller.c       # glue: post_init, housekeeping, pin table
    ├── orbit_controller.h       # layer enum, pin table, axis cache, module API
    ├── orbit_config.h/.c        # (planned) live-tunable config struct + EEPROM
    ├── orbit_logic.h/.c         # (planned) PORTED from orbit_logic.h (fixed-point math)
    ├── orbit_axes.c             # (planned) 8-channel → 6DOF pipeline (inventory §3)
    ├── orbit_chords.c           # (planned) PORTED from orbit_buttons.h
    ├── orbit_slicer.c           # (planned) pointing_device driver + wheel repeat
    ├── orbit_6dof.c             # (planned) multi-axis report assembly (Reports 1/2/3)
    ├── orbit_via.c              # (planned) via_custom_value_command_kb()
    ├── orbit_leds.c             # (planned) TX/RX LED blink state machines
    └── keymaps/
        ├── default/keymap.c     # 3 buttons × 3 layers, no VIA
        └── via/
            ├── keymap.c
            ├── config.h         # DYNAMIC_KEYMAP_LAYER_COUNT 3, MACRO_COUNT 0
            └── rules.mk         # VIA_ENABLE = yes
```

Plus, outside the keyboard folder:

```
hackman3d_orbit_controller.json  # VIA keyboard definition (menus) — lives in qmk/via/ in the
                                 # docs repo, loaded via VIA's Design tab
```

And the core patch (see `03_HID_DESCRIPTOR_FORK.md`):

```
tmk_core/protocol/usb_descriptor.c   # Multi-axis Controller descriptor
tmk_core/protocol/usb_descriptor.h   # report size
tmk_core/protocol/report.h           # report structs
tmk_core/protocol/lufa/lufa.c        # split-report send
```

**Rationale for splitting into many small `.c` files:** the Arduino version already
separated pure math (`orbit_logic.h`) from hardware, and that separation is what makes the
math unit-testable on a PC. Keeping it means we can build `orbit_logic.c` + `orbit_axes.c`
with plain `gcc` in a test harness and diff outputs against the Arduino version — the single
most valuable verification tool for this port (see `06_TASKLIST.md` Phase 0).

---

## 2. Pin mapping (Arduino → QMK)

### Analog channels

The Arduino `pins[8]` order **must be preserved**, because `v[]` indices carry meaning:

| `v[]` idx | Arduino | 32U4 port | ADC ch | QMK pin |
|---|---|---|---|---|
| 0 | A1 | PF6 | ADC6 | `F6` |
| 1 | A0 | PF7 | ADC7 | `F7` |
| 2 | A3 | PF4 | ADC4 | `F4` |
| 3 | A2 | PF5 | ADC5 | `F5` |
| 4 | A7 | PD7 | ADC10 | `D7` |
| 5 | A6 | PD4 | ADC8 | `D4` |
| 6 | A9 | PB5 | ADC12 | `B5` |
| 7 | A8 | PB4 | ADC11 | `B4` |

```c
static const pin_t orbit_analog_pins[8] = { F6, F7, F4, F5, D7, D4, B5, B4 };
```

> ⚠️ Note A0↔A1 and A2↔A3 look transposed vs. the naive reading. On the Pro Micro,
> A0 = PF7 and A1 = PF6 — the Arduino analog numbering runs *opposite* to the port bit
> numbering. The table above is correct; do not "fix" it.

QMK reads these with `analogReadPin(pin)`, which returns 10-bit (0–1023), identical to
`analogRead()`. **All calibration constants and deadzones carry over unchanged.**

### Digital

| Function | Arduino | 32U4 | QMK |
|---|---|---|---|
| Button 1 | D2 | PD1 | `D1` |
| Button 2 | D3 | PD0 | `D0` |
| Button 3 | D7 | PE6 | `E6` |
| TX LED | 30 | PD5 | `D5` (active low) |
| RX LED | 17 | PB0 | `B0` (active low) |

---

## 3. `keyboard.json`

```json
{
    "manufacturer": "3Dconnexion",
    "keyboard_name": "SpaceMouse Pro Wireless (cabled)",
    "maintainer": "HackMan3D",
    "processor": "atmega32u4",
    "bootloader": "caterina",
    "url": "https://github.com/HackMan3D/HackMan3D-Orbit-Controller",
    "usb": {
        "vid": "0x256F",
        "pid": "0xC631",
        "device_version": "1.1.0"
    },
    "matrix_pins": {
        "direct": [["D1", "D0", "E6"]]
    },
    "features": {
        "bootmagic": false,
        "command": false,
        "console": false,
        "extrakey": false,
        "mousekey": false,
        "nkro": false,
        "joystick": true,
        "pointing_device": true
    },
    "build": {
        "lto": true
    },
    "layouts": {
        "LAYOUT": {
            "layout": [
                {"matrix": [0, 0], "x": 0, "y": 0},
                {"matrix": [0, 1], "x": 1, "y": 0},
                {"matrix": [0, 2], "x": 2, "y": 0}
            ]
        }
    }
}
```

Notes:

- **VID/PID `0x256F:0xC631`** = "SpaceMouse Pro Wireless (cabled)". This is the DIY-SpaceMouse
  ecosystem standard and is what 3DxWare binds against. The manufacturer/product strings must
  also read as 3Dconnexion.
- `"joystick": true` is what pulls in the (forked) multi-axis descriptor and its endpoint.
- `extrakey`/`mousekey`/`nkro` off — we don't need media keys or mouse *keys* (we use
  `pointing_device` for real mouse deltas). Saves flash.
- `bootmagic: false` — holding a button at boot must not wipe EEPROM; our chords use all 3
  buttons.

## 4. `rules.mk`

```make
ANALOG_DRIVER_REQUIRED = yes
POINTING_DEVICE_DRIVER = custom

# Trims
MAGIC_ENABLE       = no
GRAVE_ESC_ENABLE   = no
SPACE_CADET_ENABLE = no

# Uncommented as each phase lands. orbit_controller.c is compiled
# automatically (it matches the keyboard directory name).
#SRC += orbit_logic.c \
#       orbit_config.c \
#       orbit_axes.c \
#       orbit_chords.c \
#       orbit_slicer.c \
#       orbit_6dof.c \
#       orbit_leds.c
```

LTO is set via `"build": {"lto": true}` in `keyboard.json`, not `LTO_ENABLE` in `rules.mk` —
the data-driven form is preferred in current QMK.

`orbit_via.c` is added by `keymaps/via/rules.mk` (only compiled for the VIA keymap).

## 5. `config.h`

```c
#pragma once

// ---- Joystick / multi-axis ----
#define JOYSTICK_AXIS_COUNT 6
#define JOYSTICK_AXIS_RESOLUTION 16
#define JOYSTICK_BUTTON_COUNT 32

// ---- Debounce ----
// The Arduino firmware used a 10 ms "stable" filter; QMK's default eager_pr is
// 5 ms per-row. We port the original filter explicitly in orbit_chords.c and set
// QMK's own debounce low so it doesn't add latency on top.
#define DEBOUNCE 0

// ---- Live-tuning EEPROM block ----
// orbit_config_t is ~61 bytes; 64 leaves room to grow.
#define EECONFIG_KB_DATA_SIZE 64

// ---- Pointing device (slicer mouse) ----
#define POINTING_DEVICE_TASK_THROTTLE_MS 1

// ---- Endpoint economy ----
// Do NOT define MOUSE_SHARED_EP here: POINTING_DEVICE_ENABLE already defines
// it in tmk_core/protocol/usb_descriptor.h, and QMK builds with -Werror, so
// redefining it is a hard build failure. (Found the hard way.)
#define KEYBOARD_SHARED_EP
```

`DEBOUNCE 0` deserves emphasis: we are **not** using QMK's matrix debounce for the chord
logic, because the original `readDebouncedButtons()` semantics (whole-mask stability, not
per-key) are what the chord state machine depends on. Adding QMK debounce on top would
double the latency and subtly change chord timing.

---

## 6. Execution model

The Arduino `loop()` free-runs. QMK's equivalent is `housekeeping_task_kb()`, called every
matrix scan.

```
housekeeping_task_kb()                     ← the old loop() body
├── orbit_buttons_task()                   ← readDebouncedButtons + chord predicates
│     └── chord filters, updateSpeedMode, updateSlicerMouseMode
├── orbit_axes_task()                      ← steps 1–14: ADC → oTX..oRZ
├── if (slicer_mode)
│     ├── orbit_6dof_send_zero()           ← sendCommand(0…) + sendButtons(0)
│     └── orbit_slicer_keys_task()         ← short/long press dispatch via keymap
│   else
│     ├── orbit_6dof_send(oRX,oRZ,oRY, oTX,oTZ,oTY)   ← the Y/Z swap
│     └── orbit_6dof_send_buttons(hidButtonMask)
└── orbit_leds_task()                      ← blink state machines

pointing_device_driver_get_report(report)   ← slicer mouse deltas + wheel
└── reads the axis values cached by orbit_axes_task()

keyboard_post_init_kb()                     ← the old setup()
├── LED pins output, both off
├── orbit_config_load()                     ← eeconfig kb datablock
├── wait_ms(800)
├── orbit_calibrate_center()                ← 100 samples × 5 ms + sanity check
└── orbit_leds_signal_speed_mode()
```

### Timing budget

8× `analogReadPin()` ≈ 8 × 112 µs ≈ **0.9 ms** per scan. The whole fixed-point pipeline is
integer-only (no `pow()`, no float — see Inventory Trap 1) so it costs single-digit
microseconds. Two 6-byte interrupt-endpoint writes per scan. Target report rate is the
SpaceMouse-typical **~125 Hz**, i.e. an 8 ms budget — we have ~7 ms of slack.

If the scan rate turns out too *high* (flooding the host), throttle
`orbit_6dof_send()` to 8 ms rather than slowing the whole matrix scan.

### Pointing device coupling

`orbit_axes_task()` writes the smoothed outputs to a module-level cache. The pointing device
driver callback then consumes that cache. It must **not** re-run the pipeline — the pipeline
is stateful (smoothing accumulators) and running it twice per scan would double the smoothing
rate.

---

## 7. Feature mapping

| Arduino | QMK |
|---|---|
| `analogRead(pins[i])` | `analogReadPin(orbit_analog_pins[i])` |
| `millis()` | `timer_read32()` |
| `delay(n)` | `wait_ms(n)` (setup only — never in the hot loop) |
| `digitalWrite(LED, LOW)` | `gpio_write_pin_low(D5)` |
| `pinMode(p, OUTPUT)` | `gpio_set_pin_output(p)` |
| `pinMode(p, INPUT_PULLUP)` | handled by `matrix_pins.direct` |
| `EEPROM.read/update` | `eeconfig_read_kb_datablock()` / `eeconfig_update_kb_datablock()` |
| `HID().SendReport(1/2/3, …)` | forked `send_joystick()` → 3 endpoint writes |
| `SlicerMouseHID.sendReport()` | `pointing_device_set_report()` / `pointing_device_send()` |
| `SlicerMouseHID.sendKeyboardReport()` | `tap_code16()` via dynamic keymap lookup |
| `setup()` | `keyboard_post_init_kb()` |
| `loop()` | `housekeeping_task_kb()` |
| 12 shortcut constants | dynamic keymap layers (VIA-editable) |
| `currentSpeedMode` / `slicerMouseModeEnabled` | `orbit_config_t` in eeconfig kb datablock |

---

## 8. Keymap design — how live remapping works

The 3 buttons × {short, long} = 6 actions need **three layers**, because a keymap cell holds
only one keycode:

```c
enum orbit_layers {
    _BASE        = 0,  // 6DOF mode
    _SLICER      = 1,  // slicer mode, short-press actions
    _SLICER_LONG = 2,  // slicer mode, long-press actions
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    // 6DOF mode: buttons go into the multi-axis button report (Report ID 3),
    // assembled directly by orbit_6dof.c, so these cells are placeholders.
    [_BASE]        = LAYOUT(KC_NO, KC_NO, KC_NO),

    // Slicer short press: Tab / N / Ctrl+0        (VIA-editable)
    [_SLICER]      = LAYOUT(KC_TAB, KC_N, LCTL(KC_0)),

    // Slicer long press: Shift+Alt+G / L / A      (VIA-editable)
    [_SLICER_LONG] = LAYOUT(LSA(KC_G), KC_L, KC_A),
};
```

Note `_BASE` uses `KC_NO`, not `JS_0`/`JS_1`/`JS_2`. The 6DOF buttons are not routed through
the keymap at all — `orbit_6dof.c` builds the 32-bit button mask from the filtered chord
output and sends it in Report 3, exactly as `sendButtons()` did. Routing them through QMK's
joystick keycodes would bypass the chord suppression logic.

`orbit_slicer.c` implements the 650 ms long-press timer and, on fire, looks up the keycode
from the appropriate layer and calls `tap_code16()`:

```c
uint16_t kc = keymap_key_to_keycode(long_press ? _SLICER_LONG : _SLICER,
                                    (keypos_t){.row = 0, .col = button_index});
tap_code16(kc);
```

`keymap_key_to_keycode()` reads the **dynamic** keymap when VIA is enabled, so editing the
keymap in VIA changes the behavior immediately with no reflash. That is the mechanism that
replaces all 12 `SLICER_SHORTCUT_*` constants.

`DYNAMIC_KEYMAP_LAYER_COUNT = 3` (or 4 for a spare). With a 1×3 matrix each layer is
3 keys × 2 bytes = **6 bytes** — dynamic keymaps are essentially free in EEPROM here
(see `05_SIZE_BUDGET.md`).

Layer switching is driven by the slicer-mode flag in `orbit_slicer.c`
(`layer_on(_SLICER)` / `layer_off(_SLICER)`), not by a keymap `MO()`.

---

## 9. Open design decisions

| # | Question | Recommendation |
|---|---|---|
| 1 | Fork QMK core, or vendor the descriptor into the keyboard folder? | **Fork.** No hook exists for keyboard-supplied HID interfaces; see `03`. Keep the patch on a branch of the QMK fork. |
| 2 | Keep the Arduino chord state machine, or use QMK combos? | **Keep it.** QMK combos can't express the exact-match/subset asymmetry or emit-on-release. Behavior parity > idiomatic QMK. |
| 3 | Should the slicer mouse use `pointing_device` or raw mouse reports? | `pointing_device` with `POINTING_DEVICE_DRIVER = custom`. |
| 4 | Expose per-mode speed scale + curve index in VIA? | Yes — 6 values, cheap, and highly useful for tuning. |
| 5 | Expose calibration trigger as a VIA button? | Yes — "Recalibrate" as a custom value the GUI can poke. Very useful. |
| 6 | `CONSOLE_ENABLE` in the shipping build? | No. Debug keymap only. |
