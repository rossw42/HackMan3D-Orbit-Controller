// Mock orbit_controller.h -- host-side test shim for orbit_chords.c and
// orbit_slicer.c
//
// Lets qmk/test/chord_test.c compile the REAL
// keyboards/hackman3d/orbit_controller/orbit_chords.c -- and
// qmk/test/slicer_test.c the REAL orbit_slicer.c -- on the host (gcc)
// by supplying stand-ins for the QMK APIs they touch:
//     pin_t / D1 / D0 / E6      button pin identities
//     gpio_read_pin()           driven by the test's simulated button mask
//     timer_read32()            driven by the test's simulated clock
//     report_mouse_t etc.       QMK pointing-device / keycode types
//     orbit_* accessors         recorded by the test harness
//
// The pin values only need to be distinct; chord_test.c maps them back to
// button indexes 0..2.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h> /* abs() -- normally via QMK_KEYBOARD_H */

typedef uint8_t pin_t;

#define D1 1 /* button index 0 */
#define D0 2 /* button index 1 */
#define E6 3 /* button index 2 */

#define ORBIT_SENSOR_COUNT 8
#define ORBIT_BUTTON_COUNT 3

/* --- layers (orbit_controller.h) ---------------------------------------- */
enum orbit_layers {
    _BASE        = 0,
    _SLICER      = 1,
    _SLICER_LONG = 2,
};

/* --- axis cache (orbit_controller.h) ------------------------------------ */
typedef struct {
    int16_t tx, ty, tz;
    int16_t rx, ry, rz;
} orbit_axes_t;

extern orbit_axes_t orbit_axes; /* defined by the test harness */

/* --- QMK pointing-device / keycode types (report.h / keyboard.h) -------- */
typedef struct {
    uint8_t buttons;
    int8_t  x, y;
    int8_t  v, h;
} report_mouse_t;

#define MOUSE_BTN1 0x01

typedef struct {
    uint8_t col;
    uint8_t row;
} keypos_t;

#define KC_NO   0x0000
#define KC_TRNS 0x0001

/* mocked in slicer_test.c */
uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key);
void     tap_code16_delay(uint16_t keycode, uint16_t delay_ms);

/* --- QMK APIs mocked in chord_test.c ----------------------------------- */
bool     gpio_read_pin(pin_t pin);
uint32_t timer_read32(void);

/* --- orbit modules mocked in chord_test.c ------------------------------ */
uint8_t orbit_speed_mode(void);
void    orbit_set_speed_mode(uint8_t mode);
void    orbit_reset_smoothing(void);

bool orbit_slicer_enabled(void);
void orbit_slicer_set_enabled(bool enabled);
void orbit_slicer_release_buttons(void);

void orbit_leds_signal_speed_mode(uint8_t mode);
void orbit_leds_signal_slicer_mode(bool enabled);

/* --- under test (orbit_chords.c); mocked in slicer_test.c --------------- */
void     orbit_chords_task(void);
uint32_t orbit_hid_button_mask(void);
uint32_t orbit_raw_button_mask(void);
bool     orbit_chord_active(void);

/* --- under test (orbit_slicer.c) ----------------------------------------- */
void           orbit_slicer_keys_task(void);
bool           pointing_device_driver_init(void);
report_mouse_t pointing_device_driver_get_report(report_mouse_t mouse_report);
