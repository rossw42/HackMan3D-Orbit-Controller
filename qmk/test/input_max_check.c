// Copyright 2026 HackMan3D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Checks whether the INPUT_MAX_*_FP256 constants in orbit_logic.h correctly
// describe the real post-gain range of each axis.
//
// This was originally recorded as suspicious ("finding #4"): INPUT_MAX_RZ uses
// analogRange 1024 while INPUT_MAX_TZ uses 2048, even though both Z axes sum
// four sensor channels. This test derives the true range from the pipeline in
// Hackman3D_Orbit_Controller.ino and compares.
//
// Build & run:
//   gcc -O2 -Wall input_max_check.c -o input_max_check && ./input_max_check

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

// Gains, verbatim from orbit_logic.h
#define GAIN_TX_FP 333
#define GAIN_TY_FP 333
#define GAIN_TZ_FP 589
#define GAIN_RX_FP 461
#define GAIN_RY_FP 461
#define GAIN_RZ_FP 512

// INPUT_MAX constants, verbatim from orbit_logic.h
static const int32_t INPUT_MAX_TX_FP256 = (int32_t)(1024L * GAIN_TX_FP);
static const int32_t INPUT_MAX_TY_FP256 = (int32_t)(1024L * GAIN_TY_FP);
static const int32_t INPUT_MAX_TZ_FP256 = (int32_t)(2048L * GAIN_TZ_FP);
static const int32_t INPUT_MAX_RX_FP256 = (int32_t)(1024L * GAIN_RX_FP);
static const int32_t INPUT_MAX_RY_FP256 = (int32_t)(1024L * GAIN_RY_FP);
static const int32_t INPUT_MAX_RZ_FP256 = (int32_t)(1024L * GAIN_RZ_FP);

#define Z_ROTATION_DIVISOR 2

// A single sensor deviates at most +/-512 from its centre (10-bit ADC, centre
// nominally ~512).
#define CH 512

typedef struct {
    const char *axis;
    const char *formula;
    long        pre_gain_max;   // derived from the .ino pipeline
    int         gain_fp;
    int32_t     declared_fp256;
} axis_t;

int main(void) {
    // Pre-gain ranges, derived from Hackman3D_Orbit_Controller.ino:632-652
    const axis_t axes[] = {
        // transX = v[5] - v[1]                 -> two channels, no divisor
        {"TX", "v5 - v1",              2L * CH,                  GAIN_TX_FP, 0},
        {"TY", "v7 - v3",              2L * CH,                  GAIN_TY_FP, 0},
        // transZ = -(v0+v2+v4+v6)              -> four channels, NO divisor
        {"TZ", "-(v0+v2+v4+v6)",       4L * CH,                  GAIN_TZ_FP, 0},
        {"RX", "v4 - v0",              2L * CH,                  GAIN_RX_FP, 0},
        {"RY", "v2 - v6",              2L * CH,                  GAIN_RY_FP, 0},
        // rotZ = (v1+v3+v5+v7) / 2             -> four channels, DIVIDED BY 2
        {"RZ", "(v1+v3+v5+v7)/2", (4L * CH) / Z_ROTATION_DIVISOR, GAIN_RZ_FP, 0},
    };
    const int32_t declared[] = {
        INPUT_MAX_TX_FP256, INPUT_MAX_TY_FP256, INPUT_MAX_TZ_FP256,
        INPUT_MAX_RX_FP256, INPUT_MAX_RY_FP256, INPUT_MAX_RZ_FP256,
    };

    printf("Axis pre-gain range -> post-gain range, vs declared INPUT_MAX\n");
    printf("(a single channel deviates at most +/-%d from centre)\n\n", CH);
    printf("axis  formula            pre-gain  gain   true max   declared   verdict\n");

    int mismatches = 0;
    for (int i = 0; i < 6; i++) {
        long true_post_gain = axes[i].pre_gain_max * axes[i].gain_fp / 256;
        long declared_max   = declared[i] >> 8;
        long err = labs(true_post_gain - declared_max);
        const char *verdict = (err <= 2) ? "OK" : "MISMATCH";
        if (err > 2) mismatches++;
        printf("%-4s  %-18s %8ld  %4d   %8ld   %8ld   %s\n",
               axes[i].axis, axes[i].formula, axes[i].pre_gain_max,
               axes[i].gain_fp, true_post_gain, declared_max, verdict);
    }

    printf("\n== Conclusion ==\n");
    if (mismatches == 0) {
        printf("All six INPUT_MAX constants are CORRECT.\n\n");
        printf("Finding #4 was a false positive. INPUT_MAX_RZ legitimately uses\n");
        printf("analogRange 1024 rather than 2048 because rotZ is divided by\n");
        printf("Z_ROTATION_DIVISOR (=%d) at .ino:652, which halves the four-channel\n",
               Z_ROTATION_DIVISOR);
        printf("sum back to a two-channel-equivalent range. transZ has no such\n");
        printf("divisor, so it correctly uses 2048.\n\n");
        printf("DO NOT 'fix' this. Changing INPUT_MAX_RZ to 2048*512 would make\n");
        printf("applyResponseCurve() normalise against 4096 when the real maximum\n");
        printf("is 2048, so RZ could never exceed ~50%% normalised input:\n");
        printf("  pow(0.5, 1.6) = 0.33 -> Z-rotation would lose roughly two thirds\n");
        printf("  of its output range.\n");
    } else {
        printf("%d mismatch(es) found - investigate before porting.\n", mismatches);
    }
    return mismatches ? 1 : 0;
}
