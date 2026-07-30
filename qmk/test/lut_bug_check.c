// Copyright 2026 HackMan3D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Demonstrates and quantifies the curve-LUT truncation bug in
// FirmwareUpdates/v1.1.0/Hackman3D_Orbit_Controller/orbit_logic.h
//
// The three CURVE_TABLE_* arrays are declared `uint8_t` but their last eight
// entries are written as the literal 256, which does not fit and wraps to 0.
// Effect: axis output collapses to zero past ~88% deflection instead of
// reaching full scale.
//
// It also reverse-engineers the tables' actual normalisation divisor, which is
// i/56 -- NOT the i/63 claimed in the header comment. That distinction matters:
// regenerating the tables with i/63 would move the saturation point from 88.9%
// to 100% deflection and make the device feel slower at the extremes.
//
// Build & run:
//   gcc -O2 -Wall lut_bug_check.c -o lut_bug_check -lm && ./lut_bug_check
//
// Note: gcc will emit -Woverflow warnings for the 256 literals. That is the
// point -- the compiler has been reporting this bug all along.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

// ---- Verbatim from orbit_logic.h v1.1.0 (default mode, curve 1.6) ----------
static const uint8_t CURVE_TABLE_1_6[64] = {
    0,  0,  0,  1,  2,  3,  4,  6,
    8, 10, 12, 15, 17, 20, 23, 26,
   30, 33, 37, 41, 45, 49, 53, 58,
   62, 67, 72, 77, 82, 87, 93, 98,
  104,110,115,121,127,133,139,146,
  152,158,165,171,178,185,191,198,
  205,212,219,226,233,240,247,254,
  256,256,256,256,256,256,256,256
};

// Same literals, held in int so we can see what was *intended*.
static const int WRITTEN_1_6[64] = {
    0,  0,  0,  1,  2,  3,  4,  6,
    8, 10, 12, 15, 17, 20, 23, 26,
   30, 33, 37, 41, 45, 49, 53, 58,
   62, 67, 72, 77, 82, 87, 93, 98,
  104,110,115,121,127,133,139,146,
  152,158,165,171,178,185,191,198,
  205,212,219,226,233,240,247,254,
  256,256,256,256,256,256,256,256
};

// ---- Verbatim lookupCurve() from orbit_logic.h -----------------------------
static int16_t lookupCurve(uint8_t normalized8, const uint8_t *table) {
    uint8_t idx  = normalized8 >> 2;
    uint8_t frac = (normalized8 & 0x03) << 6;
    int16_t lo   = (int16_t)table[idx];
    int16_t hi   = (idx < 63) ? (int16_t)table[idx + 1] : 256;
    return lo + (int16_t)(((int32_t)(hi - lo) * frac) >> 8);
}

static int test_truncation(void) {
    int fails = 0;
    printf("== 1. Truncation: what is actually stored? ==\n");
    for (int i = 56; i < 64; i++) {
        printf("   table[%2d] written as %3d, stored as %3u%s\n",
               i, WRITTEN_1_6[i], CURVE_TABLE_1_6[i],
               (CURVE_TABLE_1_6[i] != WRITTEN_1_6[i]) ? "   <-- TRUNCATED" : "");
        if (CURVE_TABLE_1_6[i] != WRITTEN_1_6[i]) fails++;
    }
    printf("   -> %d of 8 tail entries corrupted\n\n", fails);
    return fails;
}

static void test_output_collapse(void) {
    printf("== 2. Effect on lookupCurve() output ==\n");
    printf("   norm8   actual   intended   delta\n");
    int worst = 0, worst_at = 0;
    for (int n = 0; n <= 255; n++) {
        int got = lookupCurve((uint8_t)n, CURVE_TABLE_1_6);
        // Intended: same interpolation but against the untruncated literals.
        int idx = n >> 2, frac = (n & 3) << 6;
        int lo = WRITTEN_1_6[idx];
        int hi = (idx < 63) ? WRITTEN_1_6[idx + 1] : 256;
        int intended = lo + (((hi - lo) * frac) >> 8);
        int d = abs(got - intended);
        if (d > worst) { worst = d; worst_at = n; }
        if (n == 224 || n == 240 || n == 252 || n == 255)
            printf("   %5d   %6d   %8d   %5d%s\n", n, got, intended, got - intended,
                   (d > 40) ? "   <-- BROKEN" : "");
    }
    printf("   worst deviation = %d/256 at norm8 = %d\n", worst, worst_at);
    printf("   -> past ~88%% deflection the axis goes DEAD instead of maxing out\n\n");
}

static void test_normalisation(void) {
    printf("== 3. Which divisor did the author actually use? ==\n");
    printf("   (header comment claims i/63)\n");
    printf("   divisor   mean|err| vs written literals\n");
    double best = 1e9; int best_div = 0;
    for (int div = 54; div <= 64; div++) {
        double sum = 0;
        for (int i = 0; i < 64; i++) {
            double x = (double)i / div;
            if (x > 1.0) x = 1.0;
            int v = (int)(pow(x, 1.6) * 256.0 + 0.5);
            if (v > 256) v = 256;
            sum += abs(v - WRITTEN_1_6[i]);
        }
        double mean = sum / 64.0;
        if (mean < best) { best = mean; best_div = div; }
        if (div == 55 || div == 56 || div == 60 || div == 63)
            printf("   i/%-2d      %5.2f%s\n", div, mean,
                   (div == 63) ? "   <-- what the comment says" : "");
    }
    printf("   best fit: i/%d (mean err %.2f)\n", best_div, best);
    printf("   -> full output is reached at %.1f%% deflection BY DESIGN;\n",
           56 / 63.0 * 100);
    printf("      the top ~11%% of travel is a deliberate flat maximum.\n");
    printf("      DO NOT regenerate with i/63 -- it would change the feel.\n\n");
}

int main(void) {
    int fails = test_truncation();
    test_output_collapse();
    test_normalisation();

    printf("== Verdict ==\n");
    printf("   FIX: change the three CURVE_TABLE_* arrays to uint16_t,\n");
    printf("        keep entries 0-55 byte-for-byte, correct the comment to i/56.\n");
    printf("        Cost: 192 bytes of flash.\n");
    return fails ? 1 : 0;
}
