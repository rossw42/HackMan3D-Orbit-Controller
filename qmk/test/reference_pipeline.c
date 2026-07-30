/*
 * reference_pipeline.c
 * Phase 0 QMK port -- plain-C reference implementation of the v1.1.0 axis pipeline.
 * Produces a CSV of golden test vectors for regression testing.
 *
 * Build:
 *   gcc -O2 -Wall reference_pipeline.c -lm -o reference_pipeline && ./reference_pipeline > golden.csv
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

static const uint16_t CURVE_TABLE_1_9[64] = {
     0,  0,  0,  0,  1,  1,  2,  3,
     4,  5,  6,  8, 10, 12, 14, 16,
    19, 22, 25, 28, 31, 35, 38, 42,
    46, 50, 55, 59, 64, 69, 74, 79,
    84, 90, 95,101,107,113,119,125,
   132,138,145,151,158,165,172,179,
   186,194,201,208,216,223,231,238,
   246,254,256,256,256,256,256,256
};

static const uint16_t CURVE_TABLE_1_6[64] = {
     0,  0,  0,  1,  2,  3,  4,  6,
     8, 10, 12, 15, 17, 20, 23, 26,
    30, 33, 37, 41, 45, 49, 53, 58,
    62, 67, 72, 77, 82, 87, 93, 98,
   104,110,115,121,127,133,139,146,
   152,158,165,171,178,185,191,198,
   205,212,219,226,233,240,247,254,
   256,256,256,256,256,256,256,256
};

static const uint16_t CURVE_TABLE_1_3[64] = {
     0,  0,  1,  2,  4,  6,  8, 10,
    13, 16, 19, 22, 25, 29, 32, 36,
    40, 44, 48, 52, 56, 61, 65, 70,
    74, 79, 84, 89, 94, 99,104,109,
   114,120,125,130,136,141,147,152,
   158,164,169,175,181,187,193,199,
   205,211,217,223,229,235,241,247,
   253,256,256,256,256,256,256,256
};

static const int16_t GAIN_TX_FP = 333;
static const int16_t GAIN_TY_FP = 333;
static const int16_t GAIN_TZ_FP = 589;
static const int16_t GAIN_RX_FP = 461;
static const int16_t GAIN_RY_FP = 461;
static const int16_t GAIN_RZ_FP = 512;

static const int16_t SPEED_SCALE_FP[3] = { 128, 179, 256 };

static const int32_t INPUT_MAX_TX_FP256 = (int32_t)(1024L * 333);
static const int32_t INPUT_MAX_TY_FP256 = (int32_t)(1024L * 333);
static const int32_t INPUT_MAX_TZ_FP256 = (int32_t)(2048L * 589);
static const int32_t INPUT_MAX_RX_FP256 = (int32_t)(1024L * 461);
static const int32_t INPUT_MAX_RY_FP256 = (int32_t)(1024L * 461);
static const int32_t INPUT_MAX_RZ_FP256 = (int32_t)(1024L * 512);

static int16_t smoothValue(int16_t current, int16_t target, int smoothDivisor) {
    int16_t delta = (int16_t)(target - current);
    if (delta == 0) return current;
    int16_t step = (int16_t)(delta / smoothDivisor);
    if (step == 0) step = (delta > 0) ? 1 : -1;
    return (int16_t)(current + step);
}

static int16_t applyGain(int16_t value, int16_t gainFP) {
    return (int16_t)((int32_t)value * gainFP >> 8);
}

static int16_t lookupCurve(uint8_t normalized8, const uint16_t* table) {
    uint8_t  idx  = normalized8 >> 2;
    uint8_t  frac = (uint8_t)((normalized8 & 0x03) << 6);
    int16_t  lo   = (int16_t)table[idx];
    int16_t  hi   = (idx < 63) ? (int16_t)table[idx + 1] : 256;
    return (int16_t)(lo + (int16_t)(((int32_t)(hi - lo) * frac) >> 8));
}

static int16_t applyResponseCurve(int16_t value, int32_t inputMaxFP256,
                                   int16_t speedScaleFP, uint8_t curveTableIdx,
                                   int deadzoneOutput) {
    if (value == 0) return 0;
    int16_t mag = (int16_t)abs((int)value);
    int16_t inputMax = (int16_t)(inputMaxFP256 >> 8);
    if (inputMax < deadzoneOutput + 1) inputMax = (int16_t)(deadzoneOutput + 1);
    if (mag < deadzoneOutput) return 0;
    if (mag > inputMax)       mag = inputMax;
    int32_t rangeFP  = (int32_t)(inputMax - deadzoneOutput);
    int32_t deltaMag = (int32_t)(mag - deadzoneOutput);
    uint8_t norm8    = (uint8_t)((deltaMag * 255) / rangeFP);
    const uint16_t* tbl;
    switch (curveTableIdx) {
        case 0:  tbl = CURVE_TABLE_1_9; break;
        case 2:  tbl = CURVE_TABLE_1_3; break;
        default: tbl = CURVE_TABLE_1_6; break;
    }
    int16_t curved256 = lookupCurve(norm8, tbl);
    int32_t maxOut   = ((int32_t)inputMax * speedScaleFP) >> 8;
    if (maxOut < deadzoneOutput) maxOut = deadzoneOutput;
    int32_t outRange = maxOut - deadzoneOutput;
    int32_t out = deadzoneOutput + ((outRange * curved256) >> 8);
    return (value < 0) ? -(int16_t)out : (int16_t)out;
}

static void applyInputDeadzone(int* values, int count, int deadzoneInput) {
    int i;
    for (i = 0; i < count; i++)
        if (abs(values[i]) < deadzoneInput) values[i] = 0;
}

static int countPositive4(int a, int b, int c, int d, int threshold) {
    int n = 0;
    if (a >  threshold) n++;
    if (b >  threshold) n++;
    if (c >  threshold) n++;
    if (d >  threshold) n++;
    return n;
}

static int countNegative4(int a, int b, int c, int d, int threshold) {
    int n = 0;
    if (a < -threshold) n++;
    if (b < -threshold) n++;
    if (c < -threshold) n++;
    if (d < -threshold) n++;
    return n;
}

static void applyOutputDeadzone_C(int16_t* x,  int16_t* y,  int16_t* z,
                                   int16_t* rx, int16_t* ry, int16_t* rz,
                                   int deadzoneOutput) {
    if (abs((int)*x)  < deadzoneOutput) *x  = 0;
    if (abs((int)*y)  < deadzoneOutput) *y  = 0;
    if (abs((int)*z)  < deadzoneOutput) *z  = 0;
    if (abs((int)*rx) < deadzoneOutput) *rx = 0;
    if (abs((int)*ry) < deadzoneOutput) *ry = 0;
    if (abs((int)*rz) < deadzoneOutput) *rz = 0;
}

static void keepOnlyDominantAxis_C(int16_t* tx, int16_t* ty, int16_t* tz,
                                    int16_t* rx, int16_t* ry, int16_t* rz) {
    int16_t values[6] = { *tx, *ty, *tz, *rx, *ry, *rz };
    int maxIndex = 0, maxValue = abs((int)values[0]);
    int i;
    for (i = 1; i < 6; i++) {
        if (abs((int)values[i]) > maxValue) {
            maxValue = abs((int)values[i]); maxIndex = i;
        }
    }
    *tx = (maxIndex == 0) ? *tx : 0;
    *ty = (maxIndex == 1) ? *ty : 0;
    *tz = (maxIndex == 2) ? *tz : 0;
    *rx = (maxIndex == 3) ? *rx : 0;
    *ry = (maxIndex == 4) ? *ry : 0;
    *rz = (maxIndex == 5) ? *rz : 0;
}

#define DEADZONE_INPUT               40
#define DEADZONE_OUTPUT              45
#define SMOOTH_DIVISOR               5
/* ROTATION_PRIORITY=0.65 - = (tPow*166) */
#define ROTATION_PRIORITY_FP         166
#define ROTATION_PRIORITY_THRESHOLD  80
#define Z_PUSHPULL_THRESHOLD_MULT    2
#define Z_ROTATION_THRESHOLD_MULT    3
#define Z_ROTATION_DIVISOR           2
#define ENABLE_DOMINANT_AXIS_FILTER  0
/* invX=false invY=false invZ=false invRX=true invRY=true invRZ=true */
#define INV_X  0
#define INV_Y  0
#define INV_Z  0
#define INV_RX 1
#define INV_RY 1
#define INV_RZ 1

static const uint8_t SPEED_MODE_CURVE_IDX[3] = { 0, 1, 2 };

static int16_t smoothTX = 0, smoothTY = 0, smoothTZ = 0;
static int16_t smoothRX = 0, smoothRY = 0, smoothRZ = 0;

static void resetSmoothing(void) {
    smoothTX = smoothTY = smoothTZ = 0;
    smoothRX = smoothRY = smoothRZ = 0;
}

typedef struct {
    int16_t tx_wire;
    int16_t ty_wire;
    int16_t tz_wire;
    int16_t rx_wire;
    int16_t ry_wire;
    int16_t rz_wire;
} PipelineOut;

static PipelineOut runPipeline(const int raw[8], const int center[8], int mode) {
    int v[8];
    int i;
    for (i = 0; i < 8; i++) v[i] = raw[i] - center[i];
    applyInputDeadzone(v, 8, DEADZONE_INPUT);
    int16_t transX = (int16_t)(v[5] - v[1]);
    int16_t transY = (int16_t)(v[7] - v[3]);
    int16_t transZ = 0;
    int16_t rotX   = (int16_t)(v[4] - v[0]);
    int16_t rotY   = (int16_t)(v[2] - v[6]);
    int16_t rotZ   = 0;
    /* Z push/pull */
    int zPushPull = v[0] + v[2] + v[4] + v[6];
    if (((countPositive4(v[0],v[2],v[4],v[6],DEADZONE_INPUT)>=3) ||
         (countNegative4(v[0],v[2],v[4],v[6],DEADZONE_INPUT)>=3)) &&
        abs(zPushPull) > DEADZONE_INPUT * Z_PUSHPULL_THRESHOLD_MULT) {
        transZ = (int16_t)(-zPushPull); transX = 0; transY = 0;
    }
    /* Z twist */
    int zTwist = v[1] + v[3] + v[5] + v[7];
    if (((countPositive4(v[1],v[3],v[5],v[7],DEADZONE_INPUT)>=3) ||
         (countNegative4(v[1],v[3],v[5],v[7],DEADZONE_INPUT)>=3)) &&
        abs(zTwist) > DEADZONE_INPUT * Z_ROTATION_THRESHOLD_MULT) {
        rotZ = (int16_t)(zTwist / Z_ROTATION_DIVISOR); rotX = 0; rotY = 0;
    }
    /* Rotation priority */
    int rPow = abs((int)rotX) + abs((int)rotY) + abs((int)rotZ);
    int tPow = abs((int)transX) + abs((int)transY) + abs((int)transZ);
    if (rPow > ROTATION_PRIORITY_THRESHOLD &&
        rPow > (int)(((int32_t)tPow * ROTATION_PRIORITY_FP) >> 8)) {
        transX = 0; transY = 0; transZ = 0;
        smoothTX = 0; smoothTY = 0; smoothTZ = 0;
    }
    /* Gains */
    transX = applyGain(transX, GAIN_TX_FP);
    transY = applyGain(transY, GAIN_TY_FP);
    transZ = applyGain(transZ, GAIN_TZ_FP);
    rotX   = applyGain(rotX,   GAIN_RX_FP);
    rotY   = applyGain(rotY,   GAIN_RY_FP);
    rotZ   = applyGain(rotZ,   GAIN_RZ_FP);
    if (ENABLE_DOMINANT_AXIS_FILTER)
        keepOnlyDominantAxis_C(&transX,&transY,&transZ,&rotX,&rotY,&rotZ);
    /* Response curve */
    int16_t sFP  = SPEED_SCALE_FP[mode];
    uint8_t cIdx = SPEED_MODE_CURVE_IDX[mode];
    transX = applyResponseCurve(transX, INPUT_MAX_TX_FP256, sFP, cIdx, DEADZONE_OUTPUT);
    transY = applyResponseCurve(transY, INPUT_MAX_TY_FP256, sFP, cIdx, DEADZONE_OUTPUT);
    transZ = applyResponseCurve(transZ, INPUT_MAX_TZ_FP256, sFP, cIdx, DEADZONE_OUTPUT);
    rotX   = applyResponseCurve(rotX,   INPUT_MAX_RX_FP256, sFP, cIdx, DEADZONE_OUTPUT);
    rotY   = applyResponseCurve(rotY,   INPUT_MAX_RY_FP256, sFP, cIdx, DEADZONE_OUTPUT);
    rotZ   = applyResponseCurve(rotZ,   INPUT_MAX_RZ_FP256, sFP, cIdx, DEADZONE_OUTPUT);
    /* Output deadzone */
    applyOutputDeadzone_C(&transX,&transY,&transZ,&rotX,&rotY,&rotZ,DEADZONE_OUTPUT);
    /* Axis inversion */
    if (INV_X)  transX = (int16_t)(-transX);
    if (INV_Y)  transY = (int16_t)(-transY);
    if (INV_Z)  transZ = (int16_t)(-transZ);
    if (INV_RX) rotX   = (int16_t)(-rotX);
    if (INV_RY) rotY   = (int16_t)(-rotY);
    if (INV_RZ) rotZ   = (int16_t)(-rotZ);
    /* Smoothing */
    smoothTX = smoothValue(smoothTX, transX, SMOOTH_DIVISOR);
    smoothTY = smoothValue(smoothTY, transY, SMOOTH_DIVISOR);
    smoothTZ = smoothValue(smoothTZ, transZ, SMOOTH_DIVISOR);
    smoothRX = smoothValue(smoothRX, rotX,   SMOOTH_DIVISOR);
    smoothRY = smoothValue(smoothRY, rotY,   SMOOTH_DIVISOR);
    smoothRZ = smoothValue(smoothRZ, rotZ,   SMOOTH_DIVISOR);
    /* Final output deadzone */
    int16_t oTX=smoothTX, oTY=smoothTY, oTZ=smoothTZ;
    int16_t oRX=smoothRX, oRY=smoothRY, oRZ=smoothRZ;
    applyOutputDeadzone_C(&oTX,&oTY,&oTZ,&oRX,&oRY,&oRZ,DEADZONE_OUTPUT);
    /* Wire order: sendCommand(oRX, oRZ, oRY, oTX, oTZ, oTY) */
    PipelineOut out;
    out.tx_wire = oTX;
    out.ty_wire = oTZ;
    out.tz_wire = oTY;
    out.rx_wire = oRX;
    out.ry_wire = oRZ;
    out.rz_wire = oRY;
    return out;
}

static void emitRow(int mode, const int raw[8], const int center[8]) {
    PipelineOut o = runPipeline(raw, center, mode);
    printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
           mode,raw[0],raw[1],raw[2],raw[3],raw[4],raw[5],raw[6],raw[7],
           (int)o.tx_wire,(int)o.ty_wire,(int)o.tz_wire,
           (int)o.rx_wire,(int)o.ry_wire,(int)o.rz_wire);
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
                resetSmoothing();
                emitRow(mode, raw, center);
            }
        }
        /* Z push/pull trigger */
        { int raw[8]={700,512,700,512,700,512,700,512};
          resetSmoothing(); emitRow(mode,raw,center); }
        /* Z twist trigger */
        { int raw[8]={512,750,512,750,512,750,512,750};
          resetSmoothing(); emitRow(mode,raw,center); }
        /* Rotation priority trigger */
        { int raw[8]={337,487,512,512,687,537,512,512};
          resetSmoothing(); emitRow(mode,raw,center); }
        /* Random (seed 42) */
        srand(42);
        for (r = 0; r < 20; r++) {
            int raw[8];
            for (i = 0; i < 8; i++) raw[i] = rand() % 1024;
            resetSmoothing();
            emitRow(mode, raw, center);
        }
    }
    return 0;
}
