/*
 * qmk_pipeline.c
 * Phase 4 gate -- compiles the PORTED pipeline (orbit_pipeline.h /
 * orbit_logic.h from the QMK keyboard folder) on the host and emits the same
 * CSV as reference_pipeline.c. The diff against golden.csv must be EMPTY.
 *
 * Build (note the -I pointing at the QMK keyboard folder):
 *   gcc -O2 -Wall -I "D:/GitHub2/qmk_firmware/keyboards/hackman3d/orbit_controller" \
 *       qmk_pipeline.c -o qmk_pipeline
 *   ./qmk_pipeline > qmk_output.csv
 *   diff golden.csv qmk_output.csv       # must be empty
 *
 * The test-vector generation below is copied verbatim from
 * reference_pipeline.c main() -- same sweeps, same seeds, same order.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "orbit_pipeline.h" /* the ported code under test */

static orbit_smooth_t g_smooth;

static void emitRow(int mode, const int raw[8], const int center[8]) {
    int16_t v[8];
    int16_t out[6];
    int i;
    for (i = 0; i < 8; i++) v[i] = (int16_t)(raw[i] - center[i]);

    orbit_pipeline_run(v, (uint8_t)mode, &g_smooth, out);

    /* Wire order: sendCommand(oRX, oRZ, oRY, oTX, oTZ, oTY)
     * out[] = { oTX, oTY, oTZ, oRX, oRY, oRZ } */
    int16_t tx_wire = out[0];
    int16_t ty_wire = out[2];
    int16_t tz_wire = out[1];
    int16_t rx_wire = out[3];
    int16_t ry_wire = out[5];
    int16_t rz_wire = out[4];

    printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
           mode, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7],
           (int)tx_wire, (int)ty_wire, (int)tz_wire,
           (int)rx_wire, (int)ry_wire, (int)rz_wire);
}

int main(void) {
    const int center[8] = { 512,512,512,512,512,512,512,512 };
    int mode, ch, rv, r, i;
    printf("mode,ch0,ch1,ch2,ch3,ch4,ch5,ch6,ch7,"
           "tx_wire,ty_wire,tz_wire,rx_wire,ry_wire,rz_wire\n");
    for (mode = 0; mode < 3; mode++) {
        /* Single-axis sweeps */
        for (ch = 0; ch < 8; ch++) {
            for (rv = 0; rv <= 1023; rv += 32) {
                int raw[8] = {512,512,512,512,512,512,512,512};
                raw[ch] = rv;
                orbit_smooth_reset(&g_smooth);
                emitRow(mode, raw, center);
            }
        }
        /* Z push/pull trigger */
        { int raw[8]={700,512,700,512,700,512,700,512};
          orbit_smooth_reset(&g_smooth); emitRow(mode,raw,center); }
        /* Z twist trigger */
        { int raw[8]={512,750,512,750,512,750,512,750};
          orbit_smooth_reset(&g_smooth); emitRow(mode,raw,center); }
        /* Rotation priority trigger */
        { int raw[8]={337,487,512,512,687,537,512,512};
          orbit_smooth_reset(&g_smooth); emitRow(mode,raw,center); }
        /* Random (seed 42) */
        srand(42);
        for (r = 0; r < 20; r++) {
            int raw[8];
            for (i = 0; i < 8; i++) raw[i] = rand() % 1024;
            orbit_smooth_reset(&g_smooth);
            emitRow(mode, raw, center);
        }
    }
    return 0;
}