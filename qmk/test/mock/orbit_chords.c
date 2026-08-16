// Copyright 2026 HackMan3D
// SPDX-License-Identifier: GPL-2.0-or-later

// orbit_chords.c -- Phase 6: button debounce, chord detection, mode toggles
//
// Verbatim port of orbit_buttons.h (v1.1.0) plus the loop()-top button
// section and updateSpeedMode()/updateSlicerMouseMode() from the .ino.
// Arduino -> QMK substitutions only:
//     digitalRead(pin) == LOW   ->  !gpio_read_pin(pin)   (INPUT_PULLUP,
//                                    pins are initialised by the direct
//                                    matrix: D1, D0, E6)
//     millis()                  ->  timer_read32()
//     unsigned long             ->  uint32_t
//     currentSpeedMode++        ->  orbit_set_speed_mode() (resets smoothing
//                                    + persists, Phase 9)
//     slicerMouseModeEnabled    ->  orbit_slicer_enabled()/set_enabled()
//                                    (persisted in g_config.flags, Phase 9)
//     ledSignal*()              ->  orbit_leds_signal_*()
//     eepromSave()              ->  orbit_config_save() inside the setters
//
// Phase 9: the timing constants and the suppress flag are live g_config
// reads (VIA channel 4 -- System).
//
// CHORD PRIORITY RULES (must not be broken by future edits):
//   - Slicer toggle fires ONLY when exactly the slicer buttons are held with
//     no other buttons pressed (buttonMask == comboMask).
//   - Speed mode fires ONLY when ALL mode-switch buttons are held.
//   - Two of the three buttons are shared between both chords.
//     The exact-match requirement for the slicer combo ensures the two chords
//     are mutually exclusive: pressing all 3 buttons cannot trigger the slicer
//     toggle because the full mask never equals the 2-button slicer mask.

#include "orbit_controller.h"
#include "orbit_config.h"

/* ========================================================================
 * Configuration (.ino:157-173; the timings live in g_config since Phase 9)
 * ===================================================================== */

/* buttonPins[] = {2, 3, 7} on the Pro Micro = D1, D0, E6. Same pins as
 * matrix_pins.direct in keyboard.json; the direct matrix initialises them
 * INPUT_PULLUP, we only read them here (buttons are active LOW). */
static const pin_t orbit_button_pins[ORBIT_BUTTON_COUNT] = { D1, D0, E6 };

static const int8_t MODE_SWITCH_BUTTONS[ORBIT_BUTTON_COUNT] = { 0, 1, 2 };
#define MODE_SWITCH_BUTTON_COUNT      3

static const int8_t SLICER_MODE_BUTTONS[ORBIT_BUTTON_COUNT] = { 1, 2, 0 };
#define SLICER_MODE_BUTTON_COUNT      2

#define ENABLE_SLICER_MOUSE_MODE      true

/* ========================================================================
 * State (.ino globals + orbit_buttons.h chord state)
 * ===================================================================== */

/* Debounce state (readDebouncedButtons) */
static uint32_t debounced_button_mask = 0;
static uint32_t last_raw_button_mask  = 0;
static uint32_t last_button_change_at = 0;

/* Mode-switch chord state (orbit_buttons.h:38-42) */
static bool     _mode_switch_chord_active          = false;
static bool     _mode_switch_chord_combo_triggered = false;
static bool     _mode_switch_buttons_forwarded     = false;
static uint32_t _mode_switch_pending_buttons       = 0;
static uint32_t _mode_switch_chord_started_at      = 0;

/* Speed-mode / slicer-mode toggle state (.ino:194-207) */
static bool     mode_switch_combo_was_pressed = false;
static bool     slicer_combo_was_pressed      = false;
static bool     slicer_toggle_handled         = false;
static uint32_t slicer_combo_started_at       = 0;
static uint32_t last_slicer_mode_switch_at    = 0;
static uint32_t last_mode_switch_at           = 0;

/* Per-scan outputs consumed by the accessors at the bottom of this file. */
static uint32_t raw_mask_cache  = 0;
static uint32_t hid_mask_cache  = 0;
static bool     chord_active_cache = false;

/* ========================================================================
 * resetModeSwitchChord() -- orbit_buttons.h:51
 * ===================================================================== */

static void reset_mode_switch_chord(void) {
    _mode_switch_chord_active          = false;
    _mode_switch_chord_combo_triggered = false;
    _mode_switch_buttons_forwarded     = false;
    _mode_switch_pending_buttons       = 0;
}

/* ========================================================================
 * readDebouncedButtons() -- orbit_buttons.h:68
 * Reads physical buttons and applies a "stable for debounceMs" filter.
 * Returns the last confirmed stable mask, preventing switch bounce from
 * triggering spurious mode changes or long-press events.
 * ===================================================================== */

static uint32_t read_debounced_buttons(void) {
    uint32_t raw = 0;

    for (uint8_t i = 0; i < ORBIT_BUTTON_COUNT; i++) {
        if (!gpio_read_pin(orbit_button_pins[i])) { /* active LOW */
            raw |= (1UL << i);
        }
    }

    if (raw != last_raw_button_mask) {
        last_raw_button_mask  = raw;
        last_button_change_at = timer_read32();
    } else if (timer_read32() - last_button_change_at >= g_config.button_debounce_ms) {
        debounced_button_mask = raw;
    }

    return debounced_button_mask;
}

/* ========================================================================
 * getButtonComboMask() -- orbit_buttons.h:98
 * ===================================================================== */

static uint32_t get_button_combo_mask(const int8_t *button_indexes,
                                      int8_t combo_count,
                                      int8_t button_count) {
    if (combo_count <= 0 || combo_count > button_count) {
        return 0;
    }

    uint32_t mask = 0;

    for (int8_t i = 0; i < combo_count; i++) {
        int8_t idx = button_indexes[i];
        if (idx < 0 || idx >= button_count) {
            return 0;
        }
        mask |= (1UL << idx);
    }

    return mask;
}

/* ========================================================================
 * isModeSwitchComboPressed() -- orbit_buttons.h:124
 * All configured mode-switch buttons pressed -- SUBSET match.
 * ===================================================================== */

static bool is_mode_switch_combo_pressed(uint32_t button_mask) {
    uint32_t combo_mask = get_button_combo_mask(MODE_SWITCH_BUTTONS,
                                                MODE_SWITCH_BUTTON_COUNT,
                                                ORBIT_BUTTON_COUNT);
    if (combo_mask == 0) return false;
    return (button_mask & combo_mask) == combo_mask;
}

/* ========================================================================
 * isSlicerModeComboPressed() -- orbit_buttons.h:142
 * ONLY the slicer-mode buttons pressed -- EXACT match (High risk #19).
 * See chord priority rules at the top of this file.
 * ===================================================================== */

static bool is_slicer_mode_combo_pressed(uint32_t button_mask) {
    if (!ENABLE_SLICER_MOUSE_MODE) return false;
    uint32_t combo_mask = get_button_combo_mask(SLICER_MODE_BUTTONS,
                                                SLICER_MODE_BUTTON_COUNT,
                                                ORBIT_BUTTON_COUNT);
    if (combo_mask == 0) return false;
    return button_mask == combo_mask;
}

/* ========================================================================
 * filterModeSwitchButtons() -- orbit_buttons.h:162
 * Suppresses mode-switch buttons while a chord is being detected.
 * Sets *combo_accepted = true when the full chord fires.
 * Includes the emit-on-release path: a chord member pressed and released
 * alone (chord never completed, window not expired) is emitted on release.
 * ===================================================================== */

static uint32_t filter_mode_switch_buttons(uint32_t button_mask,
                                           bool combo_pressed,
                                           bool suppress_buttons,
                                           uint32_t chord_window_ms,
                                           bool *combo_accepted) {
    *combo_accepted = false;

    if (!suppress_buttons) {
        *combo_accepted = combo_pressed;
        return button_mask;
    }

    uint32_t switch_mask = get_button_combo_mask(MODE_SWITCH_BUTTONS,
                                                 MODE_SWITCH_BUTTON_COUNT,
                                                 ORBIT_BUTTON_COUNT);
    if (switch_mask == 0) return button_mask;

    uint32_t now                   = timer_read32();
    uint32_t non_switch_buttons    = button_mask & ~switch_mask;
    uint32_t active_switch_buttons = button_mask & switch_mask;

    if (active_switch_buttons == 0) {
        _mode_switch_chord_active = false;

        if (!_mode_switch_chord_combo_triggered &&
            !_mode_switch_buttons_forwarded &&
            _mode_switch_pending_buttons != 0) {
            uint32_t released = _mode_switch_pending_buttons;
            reset_mode_switch_chord();
            return non_switch_buttons | released;
        }

        reset_mode_switch_chord();
        return non_switch_buttons;
    }

    if (!_mode_switch_chord_active) {
        _mode_switch_chord_active          = true;
        _mode_switch_chord_combo_triggered = false;
        _mode_switch_buttons_forwarded     = false;
        _mode_switch_pending_buttons       = active_switch_buttons;
        _mode_switch_chord_started_at      = now;
    } else {
        _mode_switch_pending_buttons |= active_switch_buttons;
    }

    if (combo_pressed) {
        _mode_switch_chord_combo_triggered = true;
        *combo_accepted = true;
        return non_switch_buttons;
    }

    if (_mode_switch_chord_combo_triggered) {
        *combo_accepted = true;
        return non_switch_buttons;
    }

    if (now - _mode_switch_chord_started_at < chord_window_ms) {
        return non_switch_buttons;
    }

    _mode_switch_buttons_forwarded = true;
    return button_mask;
}

/* ========================================================================
 * filterSlicerModeButtons() -- orbit_buttons.h:236
 * Suppresses slicer-mode combo buttons while toggling mode.
 * ===================================================================== */

static uint32_t filter_slicer_mode_buttons(uint32_t button_mask,
                                           bool combo_pressed) {
    if (!ENABLE_SLICER_MOUSE_MODE || !combo_pressed) return button_mask;

    uint32_t combo_mask = get_button_combo_mask(SLICER_MODE_BUTTONS,
                                                SLICER_MODE_BUTTON_COUNT,
                                                ORBIT_BUTTON_COUNT);
    if (combo_mask == 0) return button_mask;

    return button_mask & ~combo_mask;
}

/* ========================================================================
 * updateSpeedMode() -- .ino:352
 * Rising edge only, mode_switch_debounce lockout. orbit_set_speed_mode()
 * wraps mod 3, resets the smoothing state (== the Arduino resetSmoothing()
 * call) and persists (== eepromSave()).
 * ===================================================================== */

static void update_speed_mode(bool combo_pressed) {
    uint32_t now = timer_read32();
    if (combo_pressed && !mode_switch_combo_was_pressed &&
        now - last_mode_switch_at >= g_config.mode_switch_debounce) {
        orbit_set_speed_mode(orbit_speed_mode() + 1);
        last_mode_switch_at = now;
        orbit_leds_signal_speed_mode(orbit_speed_mode());
    }
    mode_switch_combo_was_pressed = combo_pressed;
}

/* ========================================================================
 * updateSlicerMouseMode() -- .ino:365
 * slicer_hold_ms hold-to-toggle, slicer_debounce_ms lockout, one toggle per
 * hold. orbit_slicer_set_enabled() persists (== eepromSave()).
 * ===================================================================== */

static void update_slicer_mouse_mode(bool combo_pressed) {
    uint32_t now = timer_read32();
    if (combo_pressed && !slicer_combo_was_pressed) {
        slicer_combo_started_at = now;
        slicer_toggle_handled   = false;
    }
    if (combo_pressed && !slicer_toggle_handled &&
        now - slicer_combo_started_at    >= g_config.slicer_hold_ms &&
        now - last_slicer_mode_switch_at >= g_config.slicer_debounce_ms) {
        orbit_slicer_set_enabled(!orbit_slicer_enabled());
        last_slicer_mode_switch_at = now;
        slicer_toggle_handled      = true;
        orbit_slicer_release_buttons();
        orbit_reset_smoothing();
        orbit_leds_signal_slicer_mode(orbit_slicer_enabled());
    }
    if (!combo_pressed) slicer_toggle_handled = false;
    slicer_combo_was_pressed = combo_pressed;
}

/* ========================================================================
 * orbit_chords_task() -- .ino loop():600-623, once per scan
 * ===================================================================== */

void orbit_chords_task(void) {
    uint32_t button_mask = read_debounced_buttons();

    bool mode_switch_combo_pressed = is_mode_switch_combo_pressed(button_mask);
    bool slicer_combo_pressed      = is_slicer_mode_combo_pressed(button_mask);

    bool mode_switch_combo_accepted = false;
    uint32_t hid_button_mask = filter_mode_switch_buttons(
        button_mask, mode_switch_combo_pressed,
        (g_config.flags & ORBIT_FLAG_SUPPRESS_CHORD) != 0,
        g_config.chord_window_ms, &mode_switch_combo_accepted);

    hid_button_mask = filter_slicer_mode_buttons(hid_button_mask,
                                                 slicer_combo_pressed);

    update_speed_mode(mode_switch_combo_accepted);
    update_slicer_mouse_mode(slicer_combo_pressed);

    raw_mask_cache     = button_mask;
    hid_mask_cache     = hid_button_mask;
    /* Matches the Arduino slicer-button suppression condition:
     * updateSlicerMouseButtons(mask, modeSwitchComboPressed || slicerModeComboPressed) */
    chord_active_cache = mode_switch_combo_pressed || slicer_combo_pressed;
}

/* ========================================================================
 * Accessors
 * ===================================================================== */

/* Filtered mask for HID Report 3 (== Arduino hidButtonMask). */
uint32_t orbit_hid_button_mask(void) {
    return hid_mask_cache;
}

/* Debounced but unfiltered mask (== Arduino buttonMask); the Phase 7
 * slicer key handler consumes this. */
uint32_t orbit_raw_button_mask(void) {
    return raw_mask_cache;
}

/* True while either chord is held; Phase 7 uses this to suppress slicer
 * button actions during a chord. */
bool orbit_chord_active(void) {
    return chord_active_cache;
}