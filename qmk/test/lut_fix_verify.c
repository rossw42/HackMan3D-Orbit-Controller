// Copyright 2026 HackMan3D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Verifies the curve-LUT fix by including the REAL orbit_logic.h and comparing
// its behaviour against the old (broken) uint8_t tables. Run this after
// changing the tables to uint16_t; it must report PASS.
//
// Unlike lut_bug_check.c (which embeds a frozen copy of the old table to prove
// the bug existed), this test includes the live header, so it will fail if the
// fix is ever reverted or regressed.
//
// Build & run, from this directory:
//   gcc -O2 -Wall lut_fix_verify.c -o lut_fix_verify -lm && ./lut_fix_verify
//
// Exit 0 = fix present and correct. Exit 1 = regression.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../FirmwareUpdates/v1.1.0/Hackman3D_Orbit_Controller/orbit_logic.h"

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

    printf("== 1. Table storage ==\n");
    int all_256 = 1;
    for (int i = 56; i < 64; i++) if (CURVE_TABLE_1_6[i] != 256) all_256 = 0;
    fails += check("CURVE_TABLE_1_6[56..63] all store 256 (not 0)", all_256);
    fails += check("CURVE_TABLE_1_9[63] stores 256", CURVE_TABLE_1_9[63] == 256);
    fails += check("CURVE_TABLE_1_3[63] stores 256", CURVE_TABLE_1_3[63] == 256);
    fails += check("sizeof entry is 2 bytes (uint16_t)",
                   sizeof(CURVE_TABLE_1_6[0]) == 2);

    printf("\n== 2. Monotonicity (a response curve must never go backwards) ==\n");
    int mono = 1, mono_at = -1;
    for (int n = 1; n <= 255; n++) {
        if (lookupCurve((uint8_t)n, CURVE_TABLE_1_6) <
            lookupCurve((uint8_t)(n - 1), CURVE_TABLE_1_6)) {
            mono = 0; if (mono_at < 0) mono_at = n;
        }
    }
    if (!mono) printf("   first regression at norm8=%d\n", mono_at);
    fails += check("output is monotonically non-decreasing", mono);

    printf("\n== 3. Full deflection reaches full scale ==\n");
    int16_t at_max = lookupCurve(255, CURVE_TABLE_1_6);
    printf("   lookupCurve(255) = %d (want 256)\n", at_max);
    fails += check("full deflection returns 256", at_max == 256);

    printf("\n== 4. Behaviour change vs the broken version ==\n");
    printf("   norm8   old (broken)   new (fixed)   change\n");
    for (int n = 200; n <= 255; n += 8) {
        int o = old_lookupCurve((uint8_t)n, old_table_1_6);
        int f = lookupCurve((uint8_t)n, CURVE_TABLE_1_6);
        printf("   %5d   %12d   %11d   %+6d\n", n, o, f, f - o);
    }
    printf("   -> the affected band is roughly norm8 220..255,\n");
    printf("      i.e. the top ~14%% of deflection. Below that, IDENTICAL.\n");

    printf("\n== 5. No change below the affected band (feel is preserved) ==\n");
    int lowband_same = 1, diff_at = -1;
    for (int n = 0; n < 220; n++) {
        int o = old_lookupCurve((uint8_t)n, old_table_1_6);
        int f = lookupCurve((uint8_t)n, CURVE_TABLE_1_6);
        if (o != f) { lowband_same = 0; if (diff_at < 0) diff_at = n; }
    }
    if (!lowband_same) printf("   first difference at norm8=%d\n", diff_at);
    fails += check("norm8 0..219 unchanged by the fix", lowband_same);

    printf("\n== Verdict ==\n");
    if (fails == 0)
        printf("   ALL CHECKS PASSED. Fix is correct and scoped to the top of travel.\n");
    else
        printf("   %d CHECK(S) FAILED - the fix is missing or regressed.\n", fails);
    return fails ? 1 : 0;
}
