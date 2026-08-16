// Copyright 2026 HackMan3D
// SPDX-License-Identifier: GPL-2.0-or-later

// orbit_slicer.c -- Phase 7: slicer mouse mode.
//
// Verbatim port of the .ino's slicer section (v1.1.0, lines 385-531):
//     scaleMouseAxis()            -> scale_mouse_axis()
//     scaleMouseWheel()           -> scale_mouse_wheel()      (High risk #22:
//                                    the wheel direction is INVERTED --
//                                    value > 0 scrolls -1 -- keep it.)
//     sendSlicerMouse()           -> pointing_device_driver_get_report()
//     setSlicerMouseButton() /
//     releaseSlicerMouseButtons() -> slicer_mouse_button_mask +
//                                    orbit_slicer_release_buttons()
//     updateSlicerMouseButtons()  -> orbit_slicer_keys_task()
//     runSlicerButtonAction() /
//     sendSlicerKeyboardShortcut()-> run_slicer_button_action() via
//                                    keymap_key_to_keycode() + tap_code16
//
// Arduino -> QMK substitutions:
//     millis()                          -> timer_read32()
//     SlicerMouseHID.sendReport()       -> report_mouse_t returned from
//                                          pointing_device_driver_get_report()
//                                          (POINTING_DEVICE_DRIVER = custom);
//                                          the pointing_device layer sends it
//                                          whenever it changes or moves.
//     SlicerMouseHID.sendKeyboardReport(mod, key); delay(20); release
//                                       -> tap_code16_delay(kc, 20)
//     12 SLICER_SHORTCUT_* constants    -> keymap layers _SLICER (short press)
//                                          and _SLICER_LONG (long press),
//                                          looked up with
//                                          keymap_key_to_keycode() at fire
//                                          time. With VIA (Phase 8) that reads
//                                          the DYNAMIC keymap, so the actions
//                                          become live-remappable.
//
// NOTE: the _SLICER/_SLICER_LONG layers are pure LOOKUP TABLES -- they are
// never activated with layer_on(). Activating them would make the direct
// matrix (D1/D0/E6) fire the keycodes through normal QMK processing on
// press, bypassing this module's 650 ms short/long dispatch. The base layer
// is all KC_NO precisely so the matrix stays inert.
//
// The 6DOF axis source is the orbit_axes cache written once per scan by
// orbit_axes_task() -- the pipeline is NEVER re-run here (doc 02 section 6:
// it carries smoothing state; running it twice would double the smoothing
// rate).
//
// Phase 9: every tuning constant is a live g_config read (VIA channel 3 --
// Slicer), and the enabled flag lives in g_config.flags
// (ORBIT_FLAG_SLICER_ACTIVE) so it persists across replug.
// SLICER_DEADZONE_OUTPUT (== DEADZONE_OUTPUT, .ino:43) is
// g_config.deadzone_output -- shared with the pipeline, as in the Arduino
// firmware.

#include "orbit_controller.h"
#include "orbit_config.h"

/* ========================================================================
 * State
 * ===================================================================== */

/* slicerMouseButtonMask (.ino:198) -- the auto-drag left button. */
static uint8_t slicer_mouse_button_mask = 0;

/* Wheel repeat rate-limit state (.ino:205-206) */
static uint32_t last_wheel_at        = 0;
static int8_t   last_wheel_direction = 0;

/* Per-button short/long press state (.ino:199-201) */
static bool     button_was_pressed[ORBIT_BUTTON_COUNT]  = {false, false, false};
static bool     button_long_handled[ORBIT_BUTTON_COUNT] = {false, false, false};
static uint32_t button_pressed_at[ORBIT_BUTTON_COUNT]   = {0, 0, 0};

/* ========================================================================
 * Enabled flag (Phase 6 API; backed by g_config.flags since Phase 9).
 * Setting it persists immediately -- matches the Arduino eepromSave() call
 * in updateSlicerMouseMode().
 * ===================================================================== */

bool orbit_slicer_enabled(void) {
    return (g_config.flags & ORBIT_FLAG_SLICER_ACTIVE) != 0;
}

void orbit_slicer_set_enabled(bool enabled) {
    if (enabled) {
        g_config.flags |= ORBIT_FLAG_SLICER_ACTIVE;
    } else {
        g_config.flags &= (uint8_t)~ORBIT_FLAG_SLICER_ACTIVE;
    }
    orbit_config_save();
}

/* ========================================================================
 * releaseSlicerMouseButtons() -- .ino:399
 * Clears the drag button; the pointing_device layer sends the release on
 * the next task because the report's button field changed.
 * ===================================================================== */

void orbit_slicer_release_buttons(void) {
    slicer_mouse_button_mask = 0;
}

/* ========================================================================
 * scaleMouseAxis() -- .ino:479, verbatim
 * ===================================================================== */

static int8_t scale_mouse_axis(int16_t value, int16_t divisor, int16_t max_val) {
    if (abs(value) < g_config.deadzone_output) return 0;
    int16_t s = value / divisor;
    if (s == 0) s = (value > 0) ? 1 : -1;
    if (s > max_val) s = max_val;
    if (s < -max_val) s = -max_val;
    return (int8_t)s;
}

/* ========================================================================
 * scaleMouseWheel() -- .ino:488, verbatim
 *
 * Rate-limited repeat: the interval shrinks linearly from 125 ms at the
 * threshold to 45 ms at full scale. High risk #22: the sign is INVERTED --
 * pushing the knob down (tz > 0) scrolls the wheel NEGATIVE. Do not "fix".
 * ===================================================================== */

static int8_t scale_mouse_wheel(int16_t value) {
    const int16_t thr  = (int16_t)g_config.slicer_wheel_thr;
    const int16_t full = (int16_t)g_config.slicer_wheel_full;
    int16_t mag = abs(value);
    if (mag < thr) {
        last_wheel_direction = 0;
        return 0;
    }
    uint32_t now    = timer_read32();
    int16_t  capped = (mag > full) ? full : mag;
    int16_t  range  = full - thr;
    if (range < 1) range = 1;
    uint32_t i_range  = (uint32_t)(g_config.slicer_wheel_max_ms - g_config.slicer_wheel_min_ms);
    uint32_t interval = g_config.slicer_wheel_max_ms -
                        ((uint32_t)(capped - thr) * i_range / range);
    int8_t dir = (value > 0) ? (int8_t)-g_config.slicer_max_wheel
                             : (int8_t)g_config.slicer_max_wheel;
    if (dir == last_wheel_direction && now - last_wheel_at < interval) return 0;
    last_wheel_at        = now;
    last_wheel_direction = dir;
    return dir;
}

/* ========================================================================
 * Custom pointing device driver
 *
 * pointing_device_task() calls get_report() every scan (throttled to
 * POINTING_DEVICE_TASK_THROTTLE_MS = 1) and sends the report whenever it
 * differs from the previous one or carries movement.
 * ===================================================================== */

/* The weak default returns false, which puts the pointing device layer in
 * POINTING_DEVICE_STATUS_INIT_FAILED and no report is ever generated.
 * There is no hardware to initialise, so just report success. */
bool pointing_device_driver_init(void) {
    return true;
}

/* sendSlicerMouse(oTX, oTY, oTZ, oRX, oRY, oRZ) -- .ino:504, verbatim
 * (SLICER_MOUSE_AUTO_DRAG is compile-time true, so only that path is
 * ported; the drag-free variant died with the constant).
 *
 * Axis mapping (Trap: this differs from the 6DOF report order):
 *     ry + rz -> mouse X      rx -> mouse Y       (rotation pan)
 *     tx      -> mouse X      ty -> mouse Y       (translation pan)
 *     tz      -> wheel (inverted sign, rate-limited)
 * Zoom is EXCLUSIVE: |tz| >= threshold drops the drag button and emits
 * wheel only -- the early return mirrors the Arduino control flow. */
report_mouse_t pointing_device_driver_get_report(report_mouse_t mouse_report) {
    if (!orbit_slicer_enabled()) {
        /* 6DOF mode: idle report; the button release after leaving slicer
         * mode propagates because orbit_slicer_release_buttons() cleared
         * the mask and the changed report gets sent once. */
        mouse_report.buttons = slicer_mouse_button_mask;
        return mouse_report;
    }

    int16_t tx = orbit_axes.tx, ty = orbit_axes.ty, tz = orbit_axes.tz;
    int16_t rx = orbit_axes.rx, ry = orbit_axes.ry, rz = orbit_axes.rz;

    const int16_t dz_out   = (int16_t)g_config.deadzone_output;
    const int16_t move_div = (int16_t)g_config.slicer_move_divisor;
    const int16_t max_move = (int16_t)g_config.slicer_max_move;
    /* Auto-drag toggle + configurable drag button (VIA channel 3, values
     * 9/10). With auto-drag off the drag mask simply never gets set. */
    const uint8_t drag_btn = (g_config.flags & ORBIT_FLAG_AUTO_DRAG)
                                 ? g_config.slicer_drag_button : 0;

    int16_t t_pow = abs(tx) + abs(ty);
    int16_t r_pow = abs(rx) + abs(ry) + abs(rz);
    bool    zoom  = abs(tz) >= (int16_t)g_config.slicer_wheel_thr;
    int8_t  wheel = scale_mouse_wheel(tz);
    int8_t  mouse_x = 0, mouse_y = 0;

    if (zoom) {
        slicer_mouse_button_mask = 0; /* releaseSlicerMouseButtons() */
        mouse_report.buttons     = slicer_mouse_button_mask;
        mouse_report.v           = wheel;
        return mouse_report;
    }

    if (r_pow > t_pow && r_pow > dz_out) {
        slicer_mouse_button_mask |= drag_btn;
        mouse_x = scale_mouse_axis(ry + rz, move_div, max_move);
        mouse_y = scale_mouse_axis(rx, move_div, max_move);
    } else if (t_pow > dz_out) {
        slicer_mouse_button_mask |= drag_btn;
        mouse_x = scale_mouse_axis(tx, move_div, max_move);
        mouse_y = scale_mouse_axis(ty, move_div, max_move);
    } else {
        slicer_mouse_button_mask = 0; /* releaseSlicerMouseButtons() */
    }

    mouse_report.buttons = slicer_mouse_button_mask;
    mouse_report.x       = mouse_x;
    mouse_report.y       = mouse_y;
    mouse_report.v       = wheel;
    return mouse_report;
}

/* ========================================================================
 * Slicer button shortcuts
 * ===================================================================== */

/* resetSlicerButtonActions() -- .ino:412. tap_code16 is atomic (nothing can
 * be left held), so only the edge/timer state needs clearing. */
static void reset_slicer_button_actions(void) {
    for (uint8_t i = 0; i < ORBIT_BUTTON_COUNT; i++) {
        button_was_pressed[i]  = false;
        button_long_handled[i] = false;
        button_pressed_at[i]   = 0;
    }
}

/* runSlicerButtonAction() -- .ino:449. The keycode comes from the keymap
 * layers instead of the SLICER_SHORTCUT_* constant tables; the 20 ms hold
 * matches sendSlicerKeyboardShortcut()'s delay(20). */
static void run_slicer_button_action(uint8_t button_index, bool long_press) {
    orbit_slicer_release_buttons();
    uint16_t kc = keymap_key_to_keycode(long_press ? _SLICER_LONG : _SLICER,
                                        (keypos_t){.row = 0, .col = button_index});
#ifdef CONSOLE_ENABLE
    uprintf("slicer dispatch: btn=%u long=%u kc=0x%04X\n",
            button_index, (unsigned)long_press, kc);
#endif
    if (kc != KC_NO && kc != KC_TRNS) {
        tap_code16_delay(kc, 20);
    }
}

/* updateSlicerMouseButtons() -- .ino:456, verbatim logic.
 * suppressButtons == a chord is being held (mode-switch or slicer toggle);
 * orbit_chords.c caches exactly that condition as orbit_chord_active(). */
void orbit_slicer_keys_task(void) {
    if (orbit_chord_active()) {
        orbit_slicer_release_buttons();
        reset_slicer_button_actions();
        return;
    }

    uint32_t button_mask = orbit_raw_button_mask();
    uint32_t now         = timer_read32();

    for (uint8_t i = 0; i < ORBIT_BUTTON_COUNT; i++) {
        bool pressed = (button_mask & (1UL << i)) != 0;

        if (pressed && !button_was_pressed[i]) {
            button_was_pressed[i]  = true;
            button_long_handled[i] = false;
            button_pressed_at[i]   = now;
        }
        if (pressed && !button_long_handled[i] &&
            now - button_pressed_at[i] >= g_config.slicer_long_press_ms) {
            run_slicer_button_action(i, true);
            button_long_handled[i] = true;
        }
        if (!pressed && button_was_pressed[i]) {
            if (!button_long_handled[i]) run_slicer_button_action(i, false);
            button_was_pressed[i]  = false;
            button_long_handled[i] = false;
            button_pressed_at[i]   = 0;
        }
    }
}