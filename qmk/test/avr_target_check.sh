#!/usr/bin/env bash
# Copyright 2026 HackMan3D
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Compiles orbit_logic.h for the REAL target (ATmega32U4) and reports the flash
# footprint of the three response-curve tables.
#
# The other tests in this directory run on the host, which proves the math but
# not that the code is valid for AVR. This closes that gap: the curve-LUT fix
# changed the tables from uint8_t to uint16_t, and this script confirms
#   (a) avr-g++ accepts it with -Wall -Wextra -Werror=overflow, and
#   (b) the flash cost is what we claim (+192 bytes).
#
# Requires the QMK AVR toolchain. Run from a QMK MSYS shell:
#   ./avr_target_check.sh
#
# Exit 0 = compiles clean for atmega32u4 and tables measure 384 bytes.

set -u

AVRGPP=${AVRGPP:-$(command -v avr-g++ || echo /opt/qmk/bin/avr-g++)}
HDR="$(dirname "$0")/../../FirmwareUpdates/v1.1.0/Hackman3D_Orbit_Controller/orbit_logic.h"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

if [ ! -x "$AVRGPP" ] && ! command -v "$AVRGPP" >/dev/null 2>&1; then
    echo "SKIP: avr-g++ not found (set AVRGPP=/path/to/avr-g++)"
    exit 0
fi

echo "toolchain: $("$AVRGPP" --version | head -1)"
cp "$HDR" "$TMP/hdr.h"

# A volatile index stops the compiler folding the tables away, so they are
# really emitted to flash and can be measured.
cat > "$TMP/tu.cpp" <<'EOF'
#include "hdr.h"
volatile uint8_t  idx;
volatile int16_t  sink;
int main(void) {
    sink = lookupCurve(idx, CURVE_TABLE_1_9);
    sink = lookupCurve(idx, CURVE_TABLE_1_6);
    sink = lookupCurve(idx, CURVE_TABLE_1_3);
    sink = applyResponseCurve(sink, INPUT_MAX_TZ_FP256, SPEED_SCALE_FP[1], 1, 45);
    sink = applyGain(sink, GAIN_TZ_FP);
    sink = smoothValue(0, sink, 5);
    return 0;
}
EOF

echo "--- compiling for atmega32u4 ---"
if ! "$AVRGPP" -mmcu=atmega32u4 -DF_CPU=16000000UL -Os -std=gnu++11 \
        -Wall -Wextra -Werror=overflow -I"$TMP" \
        "$TMP/tu.cpp" -o "$TMP/tu.elf"; then
    echo "FAIL: does not compile for the target"
    exit 1
fi
echo "OK: compiles clean (no overflow warnings)"

TBL=$(avr-nm -S "$TMP/tu.elf" 2>/dev/null \
      | grep -i 'CURVE_TABLE' | awk '{s+=strtonum("0x"$2)} END{print s+0}')
echo "--- flash footprint ---"
echo "three CURVE_TABLE_* arrays: ${TBL} bytes"
echo "  uint16_t: 3 x 64 x 2 = 384 expected"
echo "  uint8_t was 3 x 64 x 1 = 192, so the fix costs +192 bytes"

if [ "$TBL" -ne 384 ]; then
    echo "FAIL: expected 384 bytes of table, measured ${TBL}"
    exit 1
fi
echo "PASS"
