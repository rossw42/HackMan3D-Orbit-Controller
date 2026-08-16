// Copyright 2026 HackMan3D
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// ============================================================================
// orbit_config.h -- Phase 9: the live-tunable configuration block.
//
// PURE C -- no QMK/Arduino dependencies. Included by orbit_pipeline.h,
// orbit_chords.c, orbit_slicer.c, orbit_axes.c AND by the host harnesses
// (qmk/test/qmk_pipeline.c, chord_test.c, slicer_test.c), which define their
// own `g_config` instance from ORBIT_CONFIG_DEFAULTS. On the firmware the
// single instance lives in orbit_config.c, persisted in the EECONFIG_KB
// datablock (EECONFIG_KB_DATA_SIZE 64 in config.h).
//
// Field defaults are the v1.1.0 Arduino values, verbatim -- with all-default
// config the pipeline output must still diff EMPTY against
// qmk/test/golden.csv (Phase 4 gate re-run as the Phase 9 gate).
//
// Layout map: qmk/04_VIA_CUSTOM_MENUS.md section 3-4 (5 channels, 50 values).
// If the struct layout changes, BUMP ORBIT_CONFIG_MAGIC so stale EEPROM
// content resets to defaults instead of being read as garbage.
// ============================================================================

#include <stdint.h>
#include <stdbool.h>

/* Bump on any layout/semantics change. */
#define ORBIT_CONFIG_MAGIC 0xB1

/* g_config.flags bits */
#define ORBIT_FLAG_DOMINANT_AXIS  (1 << 0) /* keepOnlyDominantAxis() filter */
#define ORBIT_FLAG_AUTO_DRAG      (1 << 1) /* slicer auto-drag button       */
#define ORBIT_FLAG_SLICER_ACTIVE  (1 << 2) /* slicer mouse mode enabled     */
#define ORBIT_FLAG_SUPPRESS_CHORD (1 << 3) /* suppress chord member buttons */

/* g_config.invert_mask bits (X Y Z RX RY RZ) */
#define ORBIT_INV_BIT_X  (1 << 0)
#define ORBIT_INV_BIT_Y  (1 << 1)
#define ORBIT_INV_BIT_Z  (1 << 2)
#define ORBIT_INV_BIT_RX (1 << 3)
#define ORBIT_INV_BIT_RY (1 << 4)
#define ORBIT_INV_BIT_RZ (1 << 5)

typedef struct {
    uint8_t  magic;                 /* ORBIT_CONFIG_MAGIC */

    /* --- axes (VIA channel 0) --- */
    uint8_t  deadzone_input;        /* 40 */
    uint8_t  deadzone_output;       /* 45 */
    uint8_t  smooth_divisor;        /* 5; MUST be >= 1 (smoothValue divides) */
    uint16_t gain_fp[6];            /* {333,333,589,461,461,512} TX TY TZ RX RY RZ */
    uint8_t  invert_mask;           /* bit0..5 = X Y Z RX RY RZ; default 0b111000 */

    /* --- speed (VIA channel 1) --- */
    uint8_t  speed_mode;            /* 1 */
    uint16_t speed_scale_fp[3];     /* {128,179,256} */
    uint8_t  speed_curve_idx[3];    /* {0,1,2}: 0=1.9, 1=1.6, 2=1.3 */

    /* --- filters (VIA channel 2) --- */
    uint16_t rotation_priority_fp;  /* 166 (0.65 * 256) */
    uint16_t rotation_priority_thr; /* 80 */
    uint8_t  z_pushpull_mult;       /* 2 */
    uint8_t  z_rotation_mult;       /* 3 */
    uint8_t  z_rotation_div;        /* 2; MUST be >= 1 */
    uint8_t  flags;                 /* ORBIT_FLAG_* */

    /* --- slicer (VIA channel 3) --- */
    uint16_t slicer_move_divisor;   /* 120; MUST be >= 1 */
    uint16_t slicer_wheel_thr;      /* 90 */
    uint16_t slicer_wheel_full;     /* 700; MUST be > slicer_wheel_thr */
    uint8_t  slicer_max_move;       /* 12 */
    uint8_t  slicer_max_wheel;      /* 1 */
    uint8_t  slicer_wheel_min_ms;   /* 45 */
    uint8_t  slicer_wheel_max_ms;   /* 125; MUST be >= slicer_wheel_min_ms */
    uint8_t  slicer_drag_button;    /* 1=left, 2=right, 4=middle (mask value) */
    uint16_t slicer_long_press_ms;  /* 650 */

    /* --- system (VIA channel 4) --- */
    uint8_t  button_debounce_ms;    /* 10 */
    uint16_t chord_window_ms;       /* 250 */
    uint16_t mode_switch_debounce;  /* 500 */
    uint16_t slicer_hold_ms;        /* 250 */
    uint16_t slicer_debounce_ms;    /* 500 */
    uint8_t  calib_samples;         /* 100; MUST be >= 1 (division) */
    uint16_t calib_min;             /* 300 */
    uint16_t calib_max;             /* 750; MUST be > calib_min */
} __attribute__((packed)) orbit_config_t;
/* 62 bytes -- fits EECONFIG_KB_DATA_SIZE 64 with room to grow. */

/* Arduino v1.1.0 defaults, verbatim. Shared between orbit_config.c's
 * set_defaults() and the host harnesses' static instances -- single source
 * of truth so they can never drift apart. */
#define ORBIT_CONFIG_DEFAULTS                                          \
    {                                                                  \
        .magic                 = ORBIT_CONFIG_MAGIC,                   \
        .deadzone_input        = 40,                                   \
        .deadzone_output       = 45,                                   \
        .smooth_divisor        = 5,                                    \
        .gain_fp               = {333, 333, 589, 461, 461, 512},       \
        .invert_mask           = (ORBIT_INV_BIT_RX | ORBIT_INV_BIT_RY  \
                                  | ORBIT_INV_BIT_RZ),                 \
        .speed_mode            = 1,                                    \
        .speed_scale_fp        = {128, 179, 256},                      \
        .speed_curve_idx       = {0, 1, 2},                            \
        .rotation_priority_fp  = 166,                                  \
        .rotation_priority_thr = 80,                                   \
        .z_pushpull_mult       = 2,                                    \
        .z_rotation_mult       = 3,                                    \
        .z_rotation_div        = 2,                                    \
        .flags                 = (ORBIT_FLAG_AUTO_DRAG                 \
                                  | ORBIT_FLAG_SUPPRESS_CHORD),        \
        .slicer_move_divisor   = 120,                                  \
        .slicer_wheel_thr      = 90,                                   \
        .slicer_wheel_full     = 700,                                  \
        .slicer_max_move       = 12,                                   \
        .slicer_max_wheel      = 1,                                    \
        .slicer_wheel_min_ms   = 45,                                   \
        .slicer_wheel_max_ms   = 125,                                  \
        .slicer_drag_button    = 1,                                    \
        .slicer_long_press_ms  = 650,                                  \
        .button_debounce_ms    = 10,                                   \
        .chord_window_ms       = 250,                                  \
        .mode_switch_debounce  = 500,                                  \
        .slicer_hold_ms        = 250,                                  \
        .slicer_debounce_ms    = 500,                                  \
        .calib_samples         = 100,                                  \
        .calib_min             = 300,                                  \
        .calib_max             = 750,                                  \
    }

/* The one live instance. Firmware: defined in orbit_config.c, loaded from
 * EEPROM at boot. Host harnesses: defined in the test file from
 * ORBIT_CONFIG_DEFAULTS. */
extern orbit_config_t g_config;

/* orbit_config.c (firmware only; host tests stub orbit_config_save). */
void orbit_config_set_defaults(void);
void orbit_config_validate(void); /* clamp EVERY field -- a zero divisor
                                     bricks the device until reflash */
void orbit_config_load(void);
void orbit_config_save(void);