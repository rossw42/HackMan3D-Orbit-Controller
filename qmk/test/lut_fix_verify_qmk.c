// Copyright 2026 HackMan3D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Same checks as lut_fix_verify.c, but against the PORTED QMK header
// (keyboards/hackman3d/orbit_controller/orbit_logic.h) instead of the
// Arduino v1.1.x header. Phase 4 requires this to PASS.
//
// Build & run:
//   gcc -O2 -Wall -I "D:/GitHub2/qmk_firmware/keyboards/hackman3d/orbit_controller"
//       lut_fix_verify_qmk.c -o lut_fix_verify_qmk   (one line) then run it.
//
// Exit 0 = fix present and correct in the ported header. Exit 1 = regression.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "orbit_logic.h" /* the PORTED header under test */

// The OLD table as it behaved when declared uint8_t: literals >255 wrapped.
static uint8_t old_table_1_6[64];
static void build_old_table(void) {
    for (int i = 0; i < 64; i++)
        old_table_1_6[i] = (uint8_t)CURVE_TABLE_1_6[i];   // truncates 256 -> 0
}

// The OLD lookupCurve(), which took a uint8_t table.
static int16_t old_lookupCurve(uint8_t n, const uint8_t *t) {
    uint8_t idx = n >> 2, frac = (n & 3) << 6;
    int16_t lo = t[idx];
    int16_t hi = (idx < 63) ? t[idx + 1] : 256;
    return lo + (int16_t)(((int32_t)(hi - lo) * frac) >> 8);
}

static int check(const char *what, int cond) {
    printf("   [%s] %s\n", cond ? "PASS" : "FAIL", what);
    return cond ? 0 : 1;
}

int main(void) {
    int fails = 0;
    build_old_table();

    printf("== 1. Table storage (ported QMK header) ==\n");
    int all_256 = 1;
    for (int i = 56; i < 64; i++) if (CURVE_TABLE_1_6[i] != 256) all_256 = 0;
    fails += check("CURVE_TABLE_1_6[56..63] all store 256 (not 0)", all_256);
    fails += check("CURVE_TABLE_1_9[63] stores 256", CURVE_TABLE_1_9[63] == 256);
    fails += check("CURVE_TABLE_1_3[63] stores 256", CURVE_TABLE_1_3[63] == 256);
    fails += check("sizeof entry is 2 bytes (uint16_t)",
                   sizeof(CURVE_TABLE_1_6[0]) == 2);

    printf("\n== 2. Monotonicity ==\n");
    int mono = 1;
    for (int n = 1; n <= 255; n++) {
        if (lookupCurve((uint8_t)n, CURVE_TABLE_1_6) <
            lookupCurve((uint8_t)(n - 1), CURVE_TABLE_1_6)) {
            mono = 0;
            break;
        }
    }
    fails += check("output is monotonically non-decreasing", mono);

    printf("\n== 3. Full deflection ==\n");
    fails += check("full deflection returns 256",
                   lookupCurve(255, CURVE_TABLE_1_6) == 256);

    printf("\n== 4. Low range unchanged by the fix ==\n");
    int low_same = 1;
    for (int n = 0; n <= 219; n++) {
        if (lookupCurve((uint8_t)n, CURVE_TABLE_1_6) !=
            old_lookupCurve((uint8_t)n, old_table_1_6)) {
            low_same = 0;
            break;
        }
    }
    fails += check("norm8 0..219 unchanged by the fix", low_same);

    printf("\n%s\n", fails ? "REGRESSION -- ported header is broken" :
                             "ALL PASS -- ported header carries the LUT fix");
    return fails ? 1 : 0;
}