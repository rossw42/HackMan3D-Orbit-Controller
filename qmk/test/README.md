# QMK port — verification harness

Host-side tests. The v1.1.0 firmware deliberately keeps its math in `orbit_logic.h` with no
Arduino dependencies, which means it compiles and runs on a PC. That makes the axis pipeline
verifiable by diffing outputs instead of guessing.

Requires only `gcc` and `libm`. On Windows, run from a QMK MSYS shell.

## `lut_bug_check.c`

Proves and quantifies the one genuine malfunction found in v1.1.0: the response-curve lookup
tables are `uint8_t` but their last eight entries are written as `256`, which wraps to `0`.

```bash
gcc -O2 -Wall lut_bug_check.c -o lut_bug_check -lm && ./lut_bug_check
```

Exit code `1` means the bug is present (expected against unmodified v1.1.0). The eight
`-Woverflow` warnings gcc emits during compilation are part of the evidence — the compiler has
been flagging this all along.

Findings:

- All 8 tail entries store `0` instead of `256`.
- Axis output **collapses to zero past ~88 % deflection** instead of reaching full scale.
  Worst deviation is the entire 256-unit range.
- The tables are **not** `pow(i/63, curve)` as their comment claims — they fit `i/56`
  (mean abs error 2.81 vs 17.25). Full output at 88.9 % deflection is a deliberate design
  choice, so **regenerating the tables with `i/63` would change how the device feels.**

The correct fix is therefore narrow: change the three arrays to `uint16_t`, keep entries 0–55
byte-for-byte, correct the comment. See `../06_TASKLIST.md`.

## Planned: `reference_pipeline.c` (Phase 0)

The golden-reference harness. Compiles the *original* `orbit_logic.h` plus a transcription of
the `.ino` axis pipeline, sweeps the 8 input channels (including the combinations that trigger
Z push/pull consensus, Z twist consensus and rotation priority), and dumps
`inputs → oTX..oRZ` to CSV for all three speed modes.

The QMK port must then reproduce that CSV exactly — apart from the LUT fix above, whose effect
must be characterised rather than silently absorbed. That diff is the gate that proves no
behaviour was lost in the port.
