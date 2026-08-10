/* chord_test.c -- Phase 6 host verification of orbit_chords.c
 *
 * Compiles the REAL keyboards/hackman3d/orbit_controller/orbit_chords.c
 * (copied next to the mock header so its quoted include resolves to
 * mock/orbit_controller.h) and drives it with a simulated clock and
 * simulated button pins.
 *
 * Covers the five Phase 6 chord tests from qmk/06_TASKLIST.md, including
 * the two negative tests:
 *   1. hold each button alone   -> registers as a joystick button after the
 *                                  250 ms chord window (forward-on-timeout)
 *   2. all 3 held               -> speed mode cycles exactly once, LED blink
 *                                  signalled with the new mode, and the HID
 *                                  mask stays 0 (no spurious button presses)
 *   3. buttons 1+2 held 250 ms  -> slicer toggles once, RX LED signalled
 *   4. NEGATIVE: all 3 held     -> slicer does NOT toggle (mutual exclusion:
 *                                  exact-match requirement, High risk #19)
 *   5. quick tap of a chord member alone -> button still emitted on release
 *                                  (emit-on-release path)
 *
 * Build (from qmk/test):
 *   cp <qmk_firmware>/keyboards/hackman3d/orbit_controller/orbit_chords.c mock/
 *   cp <qmk_firmware>/keyboards/hackman3d/orbit_controller/orbit_config.h mock/
 *   gcc -Wall -Wextra -O2 -I mock -o chord_test.exe chord_test.c mock/orbit_chords.c
 */

#include <stdio.h>
#include "orbit_controller.h"
#include "orbit_config.h"

/* Phase 9: orbit_chords.c reads its timings + the suppress flag from the
 * live config block. Defaults reproduce the original compile-time constants
 * (debounce 10, window 250, lockouts 500, suppress on). */
orbit_config_t g_config = ORBIT_CONFIG_DEFAULTS;

/* ========================================================================
 * Simulated environment
 * ===================================================================== */

static uint32_t sim_now     = 10000; /* start late enough to clear lockouts */
static uint32_t sim_buttons = 0;     /* bit i = physical button i pressed  */

uint32_t timer_read32(void) {
    return sim_now;
}

/* Buttons are active LOW (INPUT_PULLUP): pressed reads false. */
bool gpio_read_pin(pin_t pin) {
    uint8_t idx = (pin == D1) ? 0 : (pin == D0) ? 1 : 2;
    return ((sim_buttons >> idx) & 1) ? false : true;
}

/* ========================================================================
 * Mocked orbit modules -- record every call for the assertions
 * ===================================================================== */

static uint8_t mock_speed_mode     = 1; /* DEFAULT_SPEED_MODE */
static int     speed_mode_changes  = 0;

uint8_t orbit_speed_mode(void) { return mock_speed_mode; }
void orbit_set_speed_mode(uint8_t mode) {
    mock_speed_mode = (uint8_t)(mode % 3);
    speed_mode_changes++;
}
void orbit_reset_smoothing(void) {}

static bool mock_slicer     = false;
static int  slicer_toggles  = 0;

bool orbit_slicer_enabled(void) { return mock_slicer; }
void orbit_slicer_set_enabled(bool enabled) {
    mock_slicer = enabled;
    slicer_toggles++;
}
void orbit_slicer_release_buttons(void) {}

static int     led_speed_signals = 0;
static uint8_t led_last_mode     = 255;
void orbit_leds_signal_speed_mode(uint8_t mode) {
    led_speed_signals++;
    led_last_mode = mode;
}

static int  led_slicer_signals = 0;
static bool led_last_slicer    = false;
void orbit_leds_signal_slicer_mode(bool enabled) {
    led_slicer_signals++;
    led_last_slicer = enabled;
}

/* ========================================================================
 * Harness
 * ===================================================================== */

static int failures = 0;

#define CHECK(cond, msg)                                        \
    do {                                                        \
        if (cond) {                                             \
            printf("  PASS: %s\n", msg);                        \
        } else {                                                \
            printf("  FAIL: %s\n", msg);                        \
            failures++;                                         \
        }                                                       \
    } while (0)

/* Advance the clock 1 ms per scan, running the chord task each scan.
 * Returns the OR of every hid mask seen (catches one-scan emissions). */
static uint32_t step_ms(uint32_t ms) {
    uint32_t seen = 0;
    for (uint32_t i = 0; i < ms; i++) {
        sim_now++;
        orbit_chords_task();
        seen |= orbit_hid_button_mask();
    }
    return seen;
}

/* Release everything and idle long enough to clear both 500 ms lockouts
 * and the chord window. */
static void settle(void) {
    sim_buttons = 0;
    step_ms(700);
}

/* ========================================================================
 * Tests
 * ===================================================================== */

/* Test 1: hold each button alone past the 250 ms chord window ->
 * forwarded as a joystick button while held; suppressed before that. */
static void test_hold_each_button(void) {
    printf("Test 1: hold each button alone -> registers after chord window\n");
    for (uint8_t i = 0; i < 3; i++) {
        settle();
        sim_buttons = (1UL << i);
        uint32_t during_window = step_ms(200);   /* debounce 10 + inside window */
        step_ms(100);                            /* window (250) now expired    */
        uint32_t held = orbit_hid_button_mask();
        sim_buttons = 0;
        step_ms(20);
        char msg[96];
        snprintf(msg, sizeof msg, "button %u suppressed inside window (hid never set)", i);
        CHECK(during_window == 0, msg);
        snprintf(msg, sizeof msg, "button %u forwarded after window (hid == 0x%lx)",
                 i, (unsigned long)(1UL << i));
        CHECK(held == (1UL << i), msg);
    }
}

/* Test 2: all 3 held -> speed mode cycles exactly once, LED signalled,
 * no spurious HID button presses for the whole hold + release. */
static void test_speed_mode_chord(void) {
    printf("Test 2: all 3 buttons -> speed mode cycles, no spurious presses\n");
    settle();
    uint8_t before   = mock_speed_mode;
    int     changes0 = speed_mode_changes;
    int     leds0    = led_speed_signals;

    sim_buttons = 0x7;
    uint32_t seen = step_ms(400);   /* hold well past window + debounce */
    sim_buttons = 0;
    seen |= step_ms(50);            /* release: nothing may be emitted  */

    CHECK(mock_speed_mode == (uint8_t)((before + 1) % 3), "speed mode advanced by 1 (wraps mod 3)");
    CHECK(speed_mode_changes == changes0 + 1, "speed mode cycled exactly once (rising edge)");
    CHECK(led_speed_signals == leds0 + 1 && led_last_mode == mock_speed_mode,
          "TX LED signalled once with the new mode");
    CHECK(seen == 0, "no spurious HID button presses during chord or on release");
}

/* Test 3: buttons 1+2 held 250 ms -> slicer toggles once, RX LED signalled,
 * combo buttons never leak into the HID mask. */
static void test_slicer_toggle(void) {
    printf("Test 3: buttons 1+2 held 250 ms -> slicer toggles, RX LED\n");
    settle();
    bool before   = mock_slicer;
    int  toggles0 = slicer_toggles;
    int  leds0    = led_slicer_signals;

    sim_buttons = 0x6;              /* buttons 1+2 = slicer combo (exact) */
    uint32_t seen = step_ms(150);   /* debounce ~10, hold < 250: no toggle yet */
    CHECK(slicer_toggles == toggles0, "no toggle before the 250 ms hold");
    seen |= step_ms(250);           /* now past the 250 ms hold-to-toggle */
    CHECK(mock_slicer == !before, "slicer mode toggled");
    CHECK(slicer_toggles == toggles0 + 1, "toggled exactly once");
    CHECK(led_slicer_signals == leds0 + 1 && led_last_slicer == mock_slicer,
          "RX LED signalled with the new state");

    seen |= step_ms(300);           /* keep holding: must NOT toggle again */
    CHECK(slicer_toggles == toggles0 + 1, "held combo does not re-toggle (one per hold)");

    sim_buttons = 0;
    seen |= step_ms(50);
    CHECK(seen == 0, "combo buttons never appear in the HID mask");

    /* toggle back off after the 500 ms lockout, so later tests start clean */
    settle();
    sim_buttons = 0x6;
    step_ms(400);
    sim_buttons = 0;
    step_ms(50);
    CHECK(mock_slicer == before, "second hold toggles back (lockout respected)");
    CHECK(led_last_slicer == before, "RX LED signalled off");
}

/* Test 4 (NEGATIVE): all 3 held must NOT trigger the slicer toggle --
 * exact-match mutual exclusion (High risk #19). */
static void test_slicer_mutual_exclusion(void) {
    printf("Test 4 (negative): all 3 buttons do NOT trigger the slicer toggle\n");
    settle();
    int toggles0 = slicer_toggles;
    sim_buttons = 0x7;
    step_ms(600);                   /* far beyond hold + lockout */
    sim_buttons = 0;
    step_ms(50);
    CHECK(slicer_toggles == toggles0, "slicer toggle never fired with all 3 held");
}

/* Test 5 (NEGATIVE / emit-on-release): quick tap of a chord member alone
 * (released inside the chord window) must still emit the button press. */
static void test_emit_on_release(void) {
    printf("Test 5: quick tap of a chord member alone -> emit-on-release\n");
    for (uint8_t i = 0; i < 3; i++) {
        settle();
        sim_buttons = (1UL << i);
        uint32_t during = step_ms(100);   /* held < 250 ms window */
        sim_buttons = 0;
        uint32_t after = step_ms(30);     /* release; catch the 1-scan emission */
        char msg[96];
        snprintf(msg, sizeof msg, "button %u suppressed while held inside window", i);
        CHECK(during == 0, msg);
        snprintf(msg, sizeof msg, "button %u emitted on release (hid saw 0x%lx)",
                 i, (unsigned long)(1UL << i));
        CHECK(after == (1UL << i), msg);
    }
}

/* ========================================================================
 * main
 * ===================================================================== */

int main(void) {
    printf("orbit_chords.c host verification (Phase 6)\n");
    printf("==========================================\n");

    test_hold_each_button();
    test_speed_mode_chord();
    test_slicer_toggle();
    test_slicer_mutual_exclusion();
    test_emit_on_release();

    printf("==========================================\n");
    if (failures == 0) {
        printf("ALL PASS\n");
        return 0;
    }
    printf("%d FAILURE(S)\n", failures);
    return 1;
}