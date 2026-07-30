# QMK port — verification harness

Host-side tests. The v1.1.0 firmware deliberately keeps its math in `orbit_logic.h` with no
Arduino dependencies, which means it compiles and runs on a PC. That makes the axis pipeline
verifiable by diffing outputs instead of guessing.

Requires only `gcc` and `libm`. On Windows, run from a QMK MSYS shell.

| Test | Purpose | Expected |
|---|---|---|
| `lut_bug_check.c` | Proves the curve-LUT bug existed (frozen copy of the old table) | exit 1 |
| `lut_fix_verify.c` | Regression test for the fix, compiled against the **live** header | exit 0 |
| `input_max_check.c` | Validates all six `INPUT_MAX_*` constants against the real pipeline | exit 0 |
| `avr_target_check.sh` | Compiles for **atmega32u4** and measures the tables' flash cost | exit 0 |

> `lut_fix_verify.c` and `input_max_check.c` need **`g++`**, not `gcc` — `orbit_logic.h` uses
> C++ reference parameters.
>
> The three `.c` tests run on the host, which proves the *math* but not that the code is valid
> for the target. `avr_target_check.sh` closes that gap.

## `lut_bug_check.c` — the bug (historical record)

Proves and quantifies the one genuine malfunction found in v1.1.0: the response-curve lookup
tables were `uint8_t` but their last eight entries are written as `256`, which wraps to `0`.
This test embeds a frozen copy of the old table, so it keeps documenting the bug even after
the fix lands.

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
byte-for-byte, correct the comment. **Applied** in commit `d3ee98f`. See `../06_TASKLIST.md`.

## `lut_fix_verify.c` — the fix (regression test)

Compiles against the **live** `orbit_logic.h`, so it fails if the fix is reverted or regressed.

```bash
g++ -O2 -Wall lut_fix_verify.c -o lut_fix_verify && ./lut_fix_verify
```

Checks: tables store 256, entry size is 2 bytes, output is monotonic, full deflection returns
256, and **normalised input 0–219 is bit-identical to the pre-fix behaviour** — that last one
is what proves the change is confined to the top of travel and doesn't alter normal-range feel.

## `input_max_check.c` — the false positive

Derives each axis's true post-gain range from the pipeline and compares it to the declared
`INPUT_MAX_*` constant. All six match.

```bash
g++ -O2 -Wall input_max_check.c -o input_max_check && ./input_max_check
```

This test exists because `INPUT_MAX_RZ` uses `1024` while `INPUT_MAX_TZ` uses `2048` even
though both Z axes sum four channels, which looks like a bug and was initially reported as
one. It isn't: `rotZ` is divided by `Z_ROTATION_DIVISOR` (=2), halving the four-channel sum
back to a two-channel-equivalent range. Raising RZ to 2048 would have cost it roughly two
thirds of its output. **Testing before editing prevented a regression here.**

## `avr_target_check.sh` — target compilation & flash cost

Builds `orbit_logic.h` with the QMK AVR toolchain for atmega32u4 under
`-Wall -Wextra -Werror=overflow`, then measures the three curve tables via `avr-nm`.

```bash
./avr_target_check.sh          # or: AVRGPP=/path/to/avr-g++ ./avr_target_check.sh
```

Measured result:

```
toolchain: avr-g++ (crosstool-NG ... qmk/qmk_toolchains) 15.2.0
OK: compiles clean (no overflow warnings)
three CURVE_TABLE_* arrays: 384 bytes     (uint16_t: 3 x 64 x 2)
  -> the fix costs +192 bytes vs the original uint8_t tables
```

This confirms the "+192 bytes" figure quoted in the docs is real, not arithmetic on paper. The
test uses a `volatile` index so the compiler cannot constant-fold the tables out of existence —
without that, `-Os` elides them entirely and the measurement reads zero.

Skips cleanly (exit 0) if no AVR toolchain is present, so it's safe in CI.

## Planned: `reference_pipeline.c` (Phase 0)

The golden-reference harness. Compiles the *original* `orbit_logic.h` plus a transcription of
the `.ino` axis pipeline, sweeps the 8 input channels (including the combinations that trigger
Z push/pull consensus, Z twist consensus and rotation priority), and dumps
`inputs → oTX..oRZ` to CSV for all three speed modes.

The QMK port must then reproduce that CSV exactly — apart from the LUT fix above, whose effect
must be characterised rather than silently absorbed. That diff is the gate that proves no
behaviour was lost in the port.
