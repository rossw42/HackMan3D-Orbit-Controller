# 04 — Live Editing via VIA Custom Menus

How the 50 tuning parameters and the button keymap become live-editable from a GUI.

---

## 1. Decision: mainline QMK + VIA v3 custom menus

Three options were evaluated:

| Option | Custom numeric UI? | Live keymap? | Verdict |
|---|---|---|---|
| **Mainline QMK + VIA custom menus** | ✅ arbitrary, hand-authored `menus` JSON | ✅ dynamic keymaps | **CHOSEN** |
| Vial custom UI | ❌ **Vial has no custom-menu mechanism** | ✅ | Rejected |
| Vial "QMK Settings" | ❌ fixed list of ~27 built-in QMK settings, not extensible | ✅ | Rejected |

Vial was investigated specifically because it's often assumed to be the more capable option
for custom UI. It is not — Vial's QMK Settings tab is a hardcoded list of QMK-internal
settings (tapping term, mod-tap behavior, etc.) and cannot be extended by a keyboard. Vial
also requires the separate `vial-qmk` fork, which would mean maintaining *two* forks
(vial-qmk + our multi-axis patch).

VIA v3 custom menus let us define any number of sliders/toggles/dropdowns in a JSON file,
bound to a firmware handler, with no upstream submission required — VIA's **Design tab**
loads a local JSON for development.

### The reference implementation

[`plodah/ploopy_viamenus`](https://github.com/plodah/ploopy_viamenus) (local copy at
`d:\GitHub2\ploopy_viamenus`) does exactly this: **~90 live-editable parameters** — DPI
presets, per-axis invert, rotation, drag-scroll divisors, pointer acceleration curves,
turbo-fire timings — on an **ATmega32U4 with the same 28 KB budget**. Its
`users/viam/ploopy_via.c` (1000 lines) is the pattern we follow.

Key learnings from that codebase:

- Multi-byte values are transmitted **big-endian** (`value_data[0]` = MSB).
- It uses `EECONFIG_USER_DATA_SIZE` + `eeconfig_read_user_datablock()` (the modern API), not
  the older `VIA_EEPROM_CUSTOM_CONFIG_SIZE`.
- `RAW_ENABLE` is implied by `VIA_ENABLE`; don't set it manually.
- VIA has no `"type": "label"`; info-only rows are faked with a dropdown bound to a dummy id.

---

## 2. Protocol

VIA's custom-menu traffic arrives on Raw HID and QMK dispatches it to:

```c
void via_custom_value_command_kb(uint8_t *data, uint8_t length);
```

Packet layout:

```
data[0] = command    (id_custom_set_value = 0x07 / id_custom_get_value = 0x08 / id_custom_save = 0x09)
data[1] = channel_id
data[2] = value_id
data[3..] = value_data
```

So the handler derives its pointers as:

```c
void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    uint8_t *command_id   = &(data[0]);
    uint8_t *channel_id   = &(data[1]);
    uint8_t *value_id     = &(data[2]);
    uint8_t *value_data   = &(data[3]);

    if (*channel_id != ORBIT_CHANNEL) return;   // not ours

    switch (*command_id) {
        case id_custom_set_value: orbit_via_set_value(value_id, value_data); break;
        case id_custom_get_value: orbit_via_get_value(value_id, value_data); break;
        case id_custom_save:      orbit_config_save();                      break;
        default:
            *command_id = id_unhandled;
            break;
    }
}
```

`id_custom_save` is a **single global save**, not per-value — the GUI sends set_value on every
slider drag (applied live, RAM only) and a save when the user is done. This is exactly the
behavior we want: instant feedback while dragging, EEPROM written once.

---

## 3. Channel & value ID map

Five channels, grouped by tuning area, so the VIA UI gets five tabs.

```c
enum orbit_via_channel {
    ORBIT_CH_AXES    = 0,   // deadzones, gains, inversion, smoothing
    ORBIT_CH_SPEED   = 1,   // speed modes, curves
    ORBIT_CH_FILTER  = 2,   // rotation priority, Z detection, dominant axis
    ORBIT_CH_SLICER  = 3,   // slicer mouse
    ORBIT_CH_SYSTEM  = 4,   // timings, calibration, actions
};
```

### Channel 0 — Axes (`ORBIT_CH_AXES`)

| value_id | Name | Type | Range | Default |
|---|---|---|---|---|
| 1 | Input deadzone | range u8 | 0–200 | 40 |
| 2 | Output deadzone | range u8 | 0–200 | 45 |
| 3 | Smooth divisor | range u8 | 1–32 | 5 |
| 4 | Gain TX (×256) | range u16 | 26–2560 | 333 |
| 5 | Gain TY | range u16 | 26–2560 | 333 |
| 6 | Gain TZ | range u16 | 26–2560 | 589 |
| 7 | Gain RX | range u16 | 26–2560 | 461 |
| 8 | Gain RY | range u16 | 26–2560 | 461 |
| 9 | Gain RZ | range u16 | 26–2560 | 512 |
| 10 | Invert X | toggle | 0/1 | 0 |
| 11 | Invert Y | toggle | 0/1 | 0 |
| 12 | Invert Z | toggle | 0/1 | 0 |
| 13 | Invert RX | toggle | 0/1 | **1** |
| 14 | Invert RY | toggle | 0/1 | **1** |
| 15 | Invert RZ | toggle | 0/1 | **1** |

### Channel 1 — Speed (`ORBIT_CH_SPEED`)

| value_id | Name | Type | Range | Default |
|---|---|---|---|---|
| 1 | Current speed mode | dropdown | 0–2 | 1 |
| 2 | Mode 0 scale (×256) | range u16 | 26–256 | 128 |
| 3 | Mode 1 scale | range u16 | 26–256 | 179 |
| 4 | Mode 2 scale | range u16 | 26–256 | 256 |
| 5 | Mode 0 curve | dropdown | 0–2 | 0 |
| 6 | Mode 1 curve | dropdown | 0–2 | 1 |
| 7 | Mode 2 curve | dropdown | 0–2 | 2 |

Curve dropdown options: `0 = 1.9 (precision)`, `1 = 1.6 (default)`, `2 = 1.3 (fast)`.

### Channel 2 — Filters (`ORBIT_CH_FILTER`)

| value_id | Name | Type | Range | Default |
|---|---|---|---|---|
| 1 | Rotation priority (×256) | range u16 | 0–512 | 166 |
| 2 | Rotation priority threshold | range u16 | 0–1000 | 80 |
| 3 | Z push/pull threshold mult | range u8 | 1–10 | 2 |
| 4 | Z rotation threshold mult | range u8 | 1–10 | 3 |
| 5 | Z rotation divisor | range u8 | 1–8 | 2 |
| 6 | Dominant axis filter | toggle | 0/1 | 0 |

### Channel 3 — Slicer (`ORBIT_CH_SLICER`)

| value_id | Name | Type | Range | Default |
|---|---|---|---|---|
| 1 | Slicer mode active | toggle | 0/1 | 0 |
| 2 | Move divisor | range u16 | 10–500 | 120 |
| 3 | Wheel threshold | range u16 | 10–1000 | 90 |
| 4 | Wheel full scale | range u16 | 100–2000 | 700 |
| 5 | Max move | range u8 | 1–64 | 12 |
| 6 | Max wheel | range u8 | 1–8 | 1 |
| 7 | Wheel min interval (ms) | range u8 | 10–250 | 45 |
| 8 | Wheel max interval (ms) | range u8 | 10–250 | 125 |
| 9 | Auto drag | toggle | 0/1 | 1 |
| 10 | Drag button | dropdown | 1/2/4 | 1 (left) |
| 11 | Long press (ms) | range u16 | 200–2000 | 650 |

### Channel 4 — System (`ORBIT_CH_SYSTEM`)

| value_id | Name | Type | Range | Default |
|---|---|---|---|---|
| 1 | Button debounce (ms) | range u8 | 0–100 | 10 |
| 2 | Chord window (ms) | range u16 | 50–2000 | 250 |
| 3 | Mode switch debounce (ms) | range u16 | 100–2000 | 500 |
| 4 | Slicer hold (ms) | range u16 | 50–2000 | 250 |
| 5 | Slicer debounce (ms) | range u16 | 100–2000 | 500 |
| 6 | Suppress chord buttons | toggle | 0/1 | 1 |
| 7 | Calibration samples | range u8 | 10–255 | 100 |
| 8 | Calibration min | range u16 | 0–1023 | 300 |
| 9 | Calibration max | range u16 | 0–1023 | 750 |
| 10 | **Recalibrate now** | toggle (momentary) | 0/1 | — |
| 11 | **Reset to defaults** | toggle (momentary) | 0/1 | — |

Values 10 and 11 are *actions*, not stored state: writing 1 triggers the action and the
firmware immediately reports 0 back. Recalibrate is genuinely useful — currently the only way
to recentre is to unplug the device.

**Total: 15 + 7 + 6 + 11 + 11 = 50 live-editable values.**

---

## 4. Config struct

```c
// orbit_config.h
typedef struct {
    // --- axes (channel 0) ---
    uint8_t  deadzone_input;        // 40
    uint8_t  deadzone_output;       // 45
    uint8_t  smooth_divisor;        // 5
    uint16_t gain_fp[6];            // {333,333,589,461,461,512}  TX TY TZ RX RY RZ
    uint8_t  invert_mask;           // bit0..5 = X Y Z RX RY RZ; default 0b111000

    // --- speed (channel 1) ---
    uint8_t  speed_mode;            // 1
    uint16_t speed_scale_fp[3];     // {128,179,256}
    uint8_t  speed_curve_idx[3];    // {0,1,2}

    // --- filters (channel 2) ---
    uint16_t rotation_priority_fp;  // 166
    uint16_t rotation_priority_thr; // 80
    uint8_t  z_pushpull_mult;       // 2
    uint8_t  z_rotation_mult;       // 3
    uint8_t  z_rotation_div;        // 2
    uint8_t  flags;                 // bit0 dominant_axis, bit1 auto_drag,
                                    // bit2 slicer_active, bit3 suppress_chord

    // --- slicer (channel 3) ---
    uint16_t slicer_move_divisor;   // 120
    uint16_t slicer_wheel_thr;      // 90
    uint16_t slicer_wheel_full;     // 700
    uint8_t  slicer_max_move;       // 12
    uint8_t  slicer_max_wheel;      // 1
    uint8_t  slicer_wheel_min_ms;   // 45
    uint8_t  slicer_wheel_max_ms;   // 125
    uint8_t  slicer_drag_button;    // 1
    uint16_t slicer_long_press_ms;  // 650

    // --- system (channel 4) ---
    uint8_t  button_debounce_ms;    // 10
    uint16_t chord_window_ms;       // 250
    uint16_t mode_switch_debounce;  // 500
    uint16_t slicer_hold_ms;        // 250
    uint16_t slicer_debounce_ms;    // 500
    uint8_t  calib_samples;         // 100
    uint16_t calib_min;             // 300
    uint16_t calib_max;             // 750
} __attribute__((packed)) orbit_config_t;
```

Size: 3 + 12 + 1 + 1 + 6 + 3 + 2 + 2 + 3 + 1 + 6 + 5 + 2 + 1 + 8 + 1 + 4 = **~61 bytes**.
Set `#define EECONFIG_KB_DATA_SIZE 64`.

Bitfields (`invert_mask`, `flags`) instead of 10 separate `bool`s saves 8 bytes of both RAM
and EEPROM, at the cost of a tiny amount of code in get/set. Worth it.

### Derived values

`INPUT_MAX_*_FP256` is `analogRange × gain_fp`, so it must be **recomputed whenever a gain
changes**, not stored:

```c
static int32_t orbit_input_max(uint8_t axis) {
    const int32_t range = (axis == 2) ? 2048L : 1024L;   // TZ sums 4 channels
    return range * g_config.gain_fp[axis];
}
```

Careful: the Arduino code uses 2048 only for `INPUT_MAX_TZ`, **not** for RZ (which uses 1024
despite also summing 4 channels). Preserve that asymmetry — see Inventory §1.5.

---

## 5. Get / set handlers

```c
// orbit_via.c
static void orbit_via_set_value(uint8_t *value_id, uint8_t *value_data) {
    switch (*value_id) {
        // --- u8 ---
        case id_deadzone_input:  g_config.deadzone_input  = value_data[0]; break;
        case id_deadzone_output: g_config.deadzone_output = value_data[0]; break;
        case id_smooth_divisor:
            // guard: divisor of 0 would divide-by-zero in smoothValue()
            g_config.smooth_divisor = value_data[0] ? value_data[0] : 1;
            break;

        // --- u16, BIG-ENDIAN on the wire ---
        case id_gain_tx:
            g_config.gain_fp[0] = ((uint16_t)value_data[0] << 8) | value_data[1];
            break;

        // --- toggles into a bitmask ---
        case id_invert_x:
            if (value_data[0]) g_config.invert_mask |=  (1 << 0);
            else               g_config.invert_mask &= ~(1 << 0);
            break;

        // --- actions ---
        case id_recalibrate:
            if (value_data[0]) orbit_calibrate_center();
            break;
        case id_reset_defaults:
            if (value_data[0]) { orbit_config_set_defaults(); orbit_config_save(); }
            break;
    }
}

static void orbit_via_get_value(uint8_t *value_id, uint8_t *value_data) {
    switch (*value_id) {
        case id_deadzone_input:  value_data[0] = g_config.deadzone_input; break;

        case id_gain_tx:
            value_data[0] = g_config.gain_fp[0] >> 8;    // MSB first
            value_data[1] = g_config.gain_fp[0] & 0xFF;
            break;

        case id_invert_x:
            value_data[0] = (g_config.invert_mask >> 0) & 1;
            break;

        case id_recalibrate:
        case id_reset_defaults:
            value_data[0] = 0;                          // actions always read 0
            break;
    }
}
```

**Big-endian is not optional** — it's what the VIA client expects, confirmed against
`ploopy_via.c`. Getting this backwards makes every 16-bit slider behave erratically.

### Validation is mandatory

Live editing means a GUI can write **any** byte value into the firmware's math parameters. At
minimum:

- `smooth_divisor` must be ≥ 1 (`smoothValue` divides by it)
- `z_rotation_div` must be ≥ 1
- `calib_max` > `calib_min`
- `slicer_wheel_full` > `slicer_wheel_thr` (the interval interpolation divides by the
  difference; the Arduino code clamps `range` to ≥ 1 — keep that guard)
- gains > 0

A bad value should be clamped, never accepted. A divide-by-zero here hangs the device and
requires a bootloader reflash to recover.

### EEPROM save / load

```c
void orbit_config_save(void) { eeconfig_update_kb_datablock(&g_config, 0, sizeof(g_config)); }

void orbit_config_load(void) {
    eeconfig_read_kb_datablock(&g_config, 0, sizeof(g_config));
    if (g_config.magic != ORBIT_CONFIG_MAGIC) {   // first boot / version change
        orbit_config_set_defaults();
        orbit_config_save();
    }
    orbit_config_validate();     // clamp everything, defensively
}

void eeconfig_init_kb(void) {   // called by QMK on EEPROM reset
    orbit_config_set_defaults();
    orbit_config_save();
    eeconfig_init_user();
}
```

Add a `magic`/`version` byte to the struct so a firmware update with a changed layout
resets cleanly instead of loading garbage. This replaces the Arduino `EEPROM_MAGIC_VALUE`
mechanism.

---

## 6. VIA definition JSON

Lives at `qmk/via/hackman3d_orbit_controller.json` in this repo — that file is the
authoritative copy (validated: 5 tabs, 50 values). Load it through VIA's **Design tab**
during development (enable "Show Design tab" in VIA settings).

The listing below is abridged for reading; the shipped file additionally splits the Slicer tab
into "Slicer mouse mode" and "Zoom / wheel" groups.

```json
{
  "name": "HackMan3D Orbit Controller",
  "vendorProductId": 628082225,
  "matrix": { "rows": 1, "cols": 3 },
  "layouts": {
    "keymap": [["0,0", "0,1", "0,2"]]
  },
  "menus": [
    {
      "label": "Axes",
      "content": [
        {
          "label": "Deadzones",
          "content": [
            { "label": "Input deadzone",  "type": "range", "options": [0, 200],
              "content": ["id_deadzone_input", 0, 1] },
            { "label": "Output deadzone", "type": "range", "options": [0, 200],
              "content": ["id_deadzone_output", 0, 2] },
            { "label": "Smoothing",       "type": "range", "options": [1, 32],
              "content": ["id_smooth_divisor", 0, 3] }
          ]
        },
        {
          "label": "Gains (x256)",
          "content": [
            { "label": "Translation X", "type": "range", "options": [26, 2560],
              "content": ["id_gain_tx", 0, 4] },
            { "label": "Translation Y", "type": "range", "options": [26, 2560],
              "content": ["id_gain_ty", 0, 5] },
            { "label": "Translation Z", "type": "range", "options": [26, 2560],
              "content": ["id_gain_tz", 0, 6] },
            { "label": "Rotation X",    "type": "range", "options": [26, 2560],
              "content": ["id_gain_rx", 0, 7] },
            { "label": "Rotation Y",    "type": "range", "options": [26, 2560],
              "content": ["id_gain_ry", 0, 8] },
            { "label": "Rotation Z",    "type": "range", "options": [26, 2560],
              "content": ["id_gain_rz", 0, 9] }
          ]
        },
        {
          "label": "Invert",
          "content": [
            { "label": "Invert X",  "type": "toggle", "content": ["id_invert_x",  0, 10] },
            { "label": "Invert Y",  "type": "toggle", "content": ["id_invert_y",  0, 11] },
            { "label": "Invert Z",  "type": "toggle", "content": ["id_invert_z",  0, 12] },
            { "label": "Invert RX", "type": "toggle", "content": ["id_invert_rx", 0, 13] },
            { "label": "Invert RY", "type": "toggle", "content": ["id_invert_ry", 0, 14] },
            { "label": "Invert RZ", "type": "toggle", "content": ["id_invert_rz", 0, 15] }
          ]
        }
      ]
    },
    {
      "label": "Speed",
      "content": [
        {
          "label": "Speed modes",
          "content": [
            { "label": "Active mode", "type": "dropdown",
              "options": [["Slow", 0], ["Default", 1], ["Fast", 2]],
              "content": ["id_speed_mode", 1, 1] },
            { "label": "Mode 0 scale", "type": "range", "options": [26, 256],
              "content": ["id_speed_scale_0", 1, 2] },
            { "label": "Mode 1 scale", "type": "range", "options": [26, 256],
              "content": ["id_speed_scale_1", 1, 3] },
            { "label": "Mode 2 scale", "type": "range", "options": [26, 256],
              "content": ["id_speed_scale_2", 1, 4] },
            { "label": "Mode 0 curve", "type": "dropdown",
              "options": [["1.9 precision", 0], ["1.6 default", 1], ["1.3 fast", 2]],
              "content": ["id_curve_0", 1, 5] },
            { "label": "Mode 1 curve", "type": "dropdown",
              "options": [["1.9 precision", 0], ["1.6 default", 1], ["1.3 fast", 2]],
              "content": ["id_curve_1", 1, 6] },
            { "label": "Mode 2 curve", "type": "dropdown",
              "options": [["1.9 precision", 0], ["1.6 default", 1], ["1.3 fast", 2]],
              "content": ["id_curve_2", 1, 7] }
          ]
        }
      ]
    },
    {
      "label": "Filters",
      "content": [
        {
          "label": "Rotation priority",
          "content": [
            { "label": "Priority (x256)", "type": "range", "options": [0, 512],
              "content": ["id_rot_priority", 2, 1] },
            { "label": "Threshold", "type": "range", "options": [0, 1000],
              "content": ["id_rot_priority_thr", 2, 2] }
          ]
        },
        {
          "label": "Z detection",
          "content": [
            { "label": "Push/pull mult", "type": "range", "options": [1, 10],
              "content": ["id_z_pushpull_mult", 2, 3] },
            { "label": "Rotation mult", "type": "range", "options": [1, 10],
              "content": ["id_z_rotation_mult", 2, 4] },
            { "label": "Rotation divisor", "type": "range", "options": [1, 8],
              "content": ["id_z_rotation_div", 2, 5] },
            { "label": "Dominant axis only", "type": "toggle",
              "content": ["id_dominant_axis", 2, 6] }
          ]
        }
      ]
    },
    {
      "label": "Slicer",
      "content": [
        {
          "label": "Slicer mouse mode",
          "content": [
            { "label": "Slicer mode active", "type": "toggle",
              "content": ["id_slicer_active", 3, 1] },
            { "label": "Move divisor", "type": "range", "options": [10, 500],
              "content": ["id_slicer_move_div", 3, 2] },
            { "label": "Wheel threshold", "type": "range", "options": [10, 1000],
              "content": ["id_slicer_wheel_thr", 3, 3] },
            { "label": "Wheel full scale", "type": "range", "options": [100, 2000],
              "content": ["id_slicer_wheel_full", 3, 4] },
            { "label": "Max move", "type": "range", "options": [1, 64],
              "content": ["id_slicer_max_move", 3, 5] },
            { "label": "Max wheel", "type": "range", "options": [1, 8],
              "content": ["id_slicer_max_wheel", 3, 6] },
            { "label": "Wheel min interval (ms)", "type": "range", "options": [10, 250],
              "content": ["id_slicer_wheel_min", 3, 7] },
            { "label": "Wheel max interval (ms)", "type": "range", "options": [10, 250],
              "content": ["id_slicer_wheel_max", 3, 8] },
            { "label": "Auto drag", "type": "toggle",
              "content": ["id_slicer_auto_drag", 3, 9] },
            { "label": "Drag button", "type": "dropdown",
              "options": [["Left", 1], ["Right", 2], ["Middle", 4]],
              "content": ["id_slicer_drag_btn", 3, 10] },
            { "label": "Long press (ms)", "type": "range", "options": [200, 2000],
              "content": ["id_slicer_long_ms", 3, 11] }
          ]
        }
      ]
    },
    {
      "label": "System",
      "content": [
        {
          "label": "Timings",
          "content": [
            { "label": "Button debounce (ms)", "type": "range", "options": [0, 100],
              "content": ["id_btn_debounce", 4, 1] },
            { "label": "Chord window (ms)", "type": "range", "options": [50, 2000],
              "content": ["id_chord_window", 4, 2] },
            { "label": "Mode switch debounce (ms)", "type": "range", "options": [100, 2000],
              "content": ["id_mode_debounce", 4, 3] },
            { "label": "Slicer hold (ms)", "type": "range", "options": [50, 2000],
              "content": ["id_slicer_hold", 4, 4] },
            { "label": "Slicer debounce (ms)", "type": "range", "options": [100, 2000],
              "content": ["id_slicer_debounce", 4, 5] },
            { "label": "Suppress chord buttons", "type": "toggle",
              "content": ["id_suppress_chord", 4, 6] }
          ]
        },
        {
          "label": "Calibration",
          "content": [
            { "label": "Samples", "type": "range", "options": [10, 255],
              "content": ["id_calib_samples", 4, 7] },
            { "label": "Valid center min", "type": "range", "options": [0, 1023],
              "content": ["id_calib_min", 4, 8] },
            { "label": "Valid center max", "type": "range", "options": [0, 1023],
              "content": ["id_calib_max", 4, 9] },
            { "label": "Recalibrate now", "type": "toggle",
              "content": ["id_recalibrate", 4, 10] },
            { "label": "Reset all to defaults", "type": "toggle",
              "content": ["id_reset_defaults", 4, 11] }
          ]
        }
      ]
    }
  ]
}
```

`vendorProductId` = `(0x256F << 16) | 0xC631` = `0x256FC631` = **628082225**.

Get this wrong and VIA simply will not recognise the device — there is no error message, the
board just never appears. Verify with:

```
python -c "print((0x256F << 16) | 0xC631)"
```

The `"content"` triple is `[<id_name>, <channel_id>, <value_id>]`. The `id_name` string is
cosmetic in the JSON but should match the C enum name for maintainability.

---

## 7. Build flags

`keyboards/hackman3d/orbit_controller/keymaps/via/rules.mk`:

```make
VIA_ENABLE = yes
SRC += orbit_via.c
```

`config.h` additions:

```c
#define DYNAMIC_KEYMAP_LAYER_COUNT 3
#define DYNAMIC_KEYMAP_MACRO_COUNT 0     // no macros — saves ~700 bytes of EEPROM
#define EECONFIG_KB_DATA_SIZE 64
```

Setting `DYNAMIC_KEYMAP_MACRO_COUNT 0` is an important EEPROM saving — the default macro
buffer would otherwise eat most of the 1 KB. See `05_SIZE_BUDGET.md`.

---

## 8. Distribution

For end users, the definition JSON must be available:

1. **Development / power users:** VIA Design tab, load the JSON from disk.
2. **General users:** submit to [`the-via/keyboards`](https://github.com/the-via/keyboards).
   Requires the firmware to be merged into QMK master first — which our core fork makes
   awkward. Realistically we ship the JSON in this repo with instructions.
3. **Best UX:** host a small web page (like the existing `Configurator/` and
   `ButtonRemapper/` tools in this repo) using WebHID to talk the same Raw HID protocol
   directly. This reuses the VIA protocol but gives us branded UI and no VIA install. Worth
   considering as a follow-up — the existing configurator HTML is already ~80% of the work.

Option 3 is attractive because this project already has `Configurator/tuning.html`. Pointing
that at Raw HID instead of a serial protocol would give live tuning with the project's own UI.
