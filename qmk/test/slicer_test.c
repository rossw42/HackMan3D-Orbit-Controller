// slicer_test.c -- host-side verification of the REAL orbit_slicer.c
//
// Phase 7 gate: compiles keyboards/hackman3d/orbit_controller/orbit_slicer.c
// (copied into mock/ like chord_test does) against mock/orbit_controller.h
// with a simulated clock, axis cache, button mask and keymap.
//
// Covers the Phase 7 tasklist rows:
//   - scaleMouseAxis / scaleMouseWheel semantics (deadzone, min step, clamp)
//   - wheel rate-limited repeat with INVERTED sign (High risk #22)
//   - zoom exclusivity (early return: buttons dropped, no x/y)
//   - auto-drag hold/release
//   - ry+rz -> mouse X, rx -> mouse Y (rotation), tx/ty (translation)
//   - short/long press shortcut dispatch incl. chord suppression
//
// Build (QMK MSYS gcc, from qmk/test):
//   cp <qmk>/keyboards/hackman3d/orbit_controller/orbit_slicer.c mock/
//   cp <qmk>/keyboards/hackman3d/orbit_controller/orbit_config.h mock/
//   gcc -Wall -Wextra -O2 -I mock -o slicer_test.exe slicer_test.c mock/orbit_slicer.c
//   ./slicer_test.exe

#include <stdio.h>
#include <string.h>
#include "orbit_controller.h"
#include "orbit_config.h"

/* ========================================================================
 * Mocks
 * ===================================================================== */

/* Phase 9: orbit_slicer.c reads all tunables + the enabled flag from the
 * live config block; defaults == the original compile-time constants. */
orbit_config_t g_config = ORBIT_CONFIG_DEFAULTS;

/* orbit_slicer_set_enabled() persists via orbit_config_save(); no EEPROM
 * on the host, so it's a no-op here. */
void orbit_config_save(void) {}

orbit_axes_t orbit_axes = {0};

static uint32_t fake_ms = 0;
uint32_t timer_read32(void) { return fake_ms; }

/* unused by orbit_slicer.c but declared in the mock header */
bool gpio_read_pin(pin_t pin) { (void)pin; return true; }

static uint32_t fake_raw_mask     = 0;
static bool     fake_chord_active = false;
uint32_t orbit_raw_button_mask(void) { return fake_raw_mask; }
bool     orbit_chord_active(void)    { return fake_chord_active; }

/* keymap: mirrors keymaps/default/keymap.c (values only need to be distinct) */
#define KC_TAB_T   0x102
#define KC_N_T     0x103
#define CTL_0_T    0x104
#define LSA_G_T    0x105
#define KC_L_T     0x106
#define KC_A_T     0x107
static const uint16_t KEYMAP[3][3] = {
    [_BASE]        = {KC_NO,    KC_NO,  KC_NO},
    [_SLICER]      = {KC_TAB_T, KC_N_T, CTL_0_T},
    [_SLICER_LONG] = {LSA_G_T,  KC_L_T, KC_A_T},
};
uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key) {
    return KEYMAP[layer][key.col];
}

/* tap recorder */
#define TAP_LOG_MAX 16
static uint16_t tap_log[TAP_LOG_MAX];
static int      tap_count = 0;
void tap_code16_delay(uint16_t keycode, uint16_t delay_ms) {
    (void)delay_ms;
    if (tap_count < TAP_LOG_MAX) tap_log[tap_count] = keycode;
    tap_count++;
}
static void clear_taps(void) { tap_count = 0; }

/* ========================================================================
 * Harness helpers
 * ===================================================================== */

static int checks = 0, failures = 0;

#define CHECK(cond, msg)                                        \
    do {                                                        \
        checks++;                                               \
        if (!(cond)) {                                          \
            failures++;                                         \
            printf("FAIL: %s (line %d)\n", msg, __LINE__);      \
        }                                                       \
    } while (0)

static void set_axes(int16_t tx, int16_t ty, int16_t tz,
                     int16_t rx, int16_t ry, int16_t rz) {
    orbit_axes.tx = tx; orbit_axes.ty = ty; orbit_axes.tz = tz;
    orbit_axes.rx = rx; orbit_axes.ry = ry; orbit_axes.rz = rz;
}

static report_mouse_t get_report(void) {
    report_mouse_t r;
    memset(&r, 0, sizeof(r));
    return pointing_device_driver_get_report(r);
}

/* Reset the module's idle state between tests: disabled + neutral axes for
 * a few "scans" so the wheel direction latch and drag mask clear. */
static void settle(void) {
    set_axes(0, 0, 0, 0, 0, 0);
    fake_raw_mask     = 0;
    fake_chord_active = false;
    orbit_slicer_set_enabled(true);
    fake_ms += 1000;
    (void)get_report();
    orbit_slicer_keys_task();
    fake_ms += 1000;
}

/* ========================================================================
 * Tests
 * ===================================================================== */

static void test_init_and_disabled(void) {
    CHECK(pointing_device_driver_init(), "driver init returns true");

    orbit_slicer_set_enabled(false);
    set_axes(500, 500, 500, 500, 500, 500);
    report_mouse_t r = get_report();
    CHECK(r.x == 0 && r.y == 0 && r.v == 0 && r.buttons == 0,
          "disabled: axes ignored, idle report");
    orbit_slicer_set_enabled(true);
}

static void test_deadzone_idle(void) {
    settle();
    set_axes(20, 20, 0, 10, 10, 0); /* every pool below DEADZONE_OUTPUT=45 */
    report_mouse_t r = get_report();
    CHECK(r.x == 0 && r.y == 0 && r.v == 0 && r.buttons == 0,
          "below output deadzone: no motion, no drag");
}

static void test_translation_pan(void) {
    settle();
    set_axes(360, -240, 0, 0, 0, 0); /* t_pow=600 dominant */
    report_mouse_t r = get_report();
    CHECK(r.x == 3,  "translation: x = tx/120 = 3");
    CHECK(r.y == -2, "translation: y = ty/120 = -2");
    CHECK(r.buttons == MOUSE_BTN1, "translation: auto-drag left held");
    CHECK(r.v == 0, "translation: no wheel");

    /* min step: |value| >= 45 but /120 == 0 -> +/-1 */
    set_axes(100, -100, 0, 0, 0, 0);
    r = get_report();
    CHECK(r.x == 1 && r.y == -1, "min step +/-1 below one divisor unit");

    /* clamp to +/-12 (values stay in pipeline range: t_pow is 16-bit on the
     * AVR, so unrealistically large inputs would overflow -- same as the
     * Arduino original) */
    set_axes(2000, -2000, 0, 0, 0, 0);
    r = get_report();
    CHECK(r.x == 12 && r.y == -12, "clamped to +/-SLICER_MOUSE_MAX_MOVE");

    /* release: axes back inside deadzone -> drag released */
    set_axes(0, 0, 0, 0, 0, 0);
    r = get_report();
    CHECK(r.buttons == 0 && r.x == 0 && r.y == 0, "idle: auto-drag released");
}

static void test_rotation_pan(void) {
    settle();
    /* r_pow=600 > t_pow=0; mouse X from ry+rz, mouse Y from rx */
    set_axes(0, 0, 0, 240, 180, 180);
    report_mouse_t r = get_report();
    CHECK(r.x == 3, "rotation: x = (ry+rz)/120 = 3");
    CHECK(r.y == 2, "rotation: y = rx/120 = 2");
    CHECK(r.buttons == MOUSE_BTN1, "rotation: auto-drag left held");

    /* rotation must win only when r_pow > t_pow */
    set_axes(700, 0, 0, 240, 180, 180); /* t_pow=700 > r_pow=600 */
    r = get_report();
    CHECK(r.x == 5 && r.y == 0, "translation wins when t_pow >= r_pow");
}

static void test_zoom_exclusive_and_inverted(void) {
    settle();
    /* strong pan AND |tz| >= 90 -> zoom takes over completely */
    set_axes(600, 600, 200, 0, 0, 0);
    report_mouse_t r = get_report();
    CHECK(r.x == 0 && r.y == 0, "zoom: early return, no pan motion");
    CHECK(r.buttons == 0, "zoom: drag button dropped");
    CHECK(r.v == -1, "zoom: tz > 0 scrolls NEGATIVE (inverted, High risk #22)");

    settle();
    set_axes(0, 0, -200, 0, 0, 0);
    r = get_report();
    CHECK(r.v == 1, "zoom: tz < 0 scrolls POSITIVE (inverted)");
}

static void test_wheel_rate_limit(void) {
    settle();
    set_axes(0, 0, 200, 0, 0, 0);
    report_mouse_t r = get_report();
    CHECK(r.v == -1, "wheel: first event fires immediately");

    /* same direction inside the interval -> suppressed.
     * tz=200: interval = 125 - (110*80/610) = 125-14 = 111 ms */
    fake_ms += 50;
    r = get_report();
    CHECK(r.v == 0, "wheel: repeat suppressed inside interval");
    fake_ms += 70; /* 120 ms since fire > 111 ms */
    r = get_report();
    CHECK(r.v == -1, "wheel: repeats after the interval");

    /* full scale -> min interval 45 ms */
    set_axes(0, 0, 700, 0, 0, 0);
    fake_ms += 45;
    r = get_report();
    CHECK(r.v == -1, "wheel: full-scale repeats at 45 ms");
    fake_ms += 40;
    r = get_report();
    CHECK(r.v == 0, "wheel: 40 ms < min interval suppressed");

    /* direction flip fires immediately (rate limit is per-direction) */
    set_axes(0, 0, -700, 0, 0, 0);
    r = get_report();
    CHECK(r.v == 1, "wheel: direction change fires immediately");

    /* drop below threshold resets the latch */
    set_axes(0, 0, 0, 0, 0, 0);
    r = get_report();
    CHECK(r.v == 0, "wheel: idle below threshold");
    set_axes(0, 0, -700, 0, 0, 0);
    r = get_report();
    CHECK(r.v == 1, "wheel: fires right after the latch reset");
}

static void test_short_and_long_press(void) {
    settle();
    clear_taps();

    /* short press on button 0: fires the _SLICER keycode on RELEASE */
    fake_raw_mask = 0x1;
    orbit_slicer_keys_task();
    CHECK(tap_count == 0, "short press: nothing on press");
    fake_ms += 100;
    orbit_slicer_keys_task();
    CHECK(tap_count == 0, "short press: nothing while held < 650 ms");
    fake_raw_mask = 0;
    orbit_slicer_keys_task();
    CHECK(tap_count == 1 && tap_log[0] == KC_TAB_T,
          "short press: _SLICER keycode fired on release");

    /* long press on button 2: fires the _SLICER_LONG keycode at 650 ms,
     * nothing more on release */
    clear_taps();
    fake_raw_mask = 0x4;
    orbit_slicer_keys_task();
    fake_ms += 650;
    orbit_slicer_keys_task();
    CHECK(tap_count == 1 && tap_log[0] == KC_A_T,
          "long press: _SLICER_LONG keycode fired at 650 ms");
    fake_ms += 100;
    orbit_slicer_keys_task();
    fake_raw_mask = 0;
    orbit_slicer_keys_task();
    CHECK(tap_count == 1, "long press: no extra fire on release");
}

static void test_chord_suppression(void) {
    settle();
    clear_taps();

    /* hold button 0, then a chord becomes active mid-hold: the pending
     * press is cancelled -- releasing later fires NOTHING */
    fake_raw_mask = 0x1;
    orbit_slicer_keys_task();
    fake_ms += 100;
    fake_chord_active = true;
    orbit_slicer_keys_task();
    fake_chord_active = false;
    fake_raw_mask     = 0;
    orbit_slicer_keys_task();
    CHECK(tap_count == 0, "chord active: pending press cancelled");

    /* drag held, then a chord: drag must be released */
    set_axes(600, 0, 0, 0, 0, 0);
    report_mouse_t r = get_report();
    CHECK(r.buttons == MOUSE_BTN1, "setup: drag held");
    fake_chord_active = true;
    orbit_slicer_keys_task();      /* releases the drag mask */
    set_axes(0, 0, 0, 0, 0, 0);
    r = get_report();
    CHECK(r.buttons == 0, "chord active: drag released");
    fake_chord_active = false;
}

static void test_release_on_mode_exit(void) {
    settle();
    set_axes(600, 0, 0, 0, 0, 0);
    report_mouse_t r = get_report();
    CHECK(r.buttons == MOUSE_BTN1, "setup: drag held in slicer mode");

    /* leaving slicer mode: housekeeping calls orbit_slicer_release_buttons()
     * every scan of the 6DOF branch */
    orbit_slicer_set_enabled(false);
    orbit_slicer_release_buttons();
    r = get_report();
    CHECK(r.buttons == 0 && r.x == 0 && r.y == 0 && r.v == 0,
          "6DOF mode: drag released, idle report");
    orbit_slicer_set_enabled(true);
}

/* ========================================================================
 * main
 * ===================================================================== */

int main(void) {
    test_init_and_disabled();
    test_deadzone_idle();
    test_translation_pan();
    test_rotation_pan();
    test_zoom_exclusive_and_inverted();
    test_wheel_rate_limit();
    test_short_and_long_press();
    test_chord_suppression();
    test_release_on_mode_exit();

    printf("%d checks, %d failures -- %s\n",
           checks, failures, failures == 0 ? "ALL PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}