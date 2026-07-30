# Firmware Update Strategy

How we will fold the 9 improvements in `FIRMWARE_IMPROVEMENTS.md` into
`Hackman3D_Orbit_Controller.ino`, while staying able to pull in future
upstream changes to that same file without a painful re-merge every time.

This document is the decision record. `FIRMWARE_IMPROVEMENTS_TASKLIST.md`
is the actionable checklist derived from it.

---

## 1. Why this needs a strategy at all

- `Hackman3D_Orbit_Controller.ino` is a single ~1,750-line file. It is also
  the file two browser tools (`Configurator/tuning.html` and
  `ButtonRemapper`) directly text-patch by matching lines like
  `const TYPE NAME = value;`. Any restructuring has to keep those patchable
  constants intact and discoverable.
- This repo has `origin` (our fork) and `upstream`
  (`HackMan3D/HackMan3D-Orbit-Controller`). As of this writing `main` and
  `upstream/main` are identical (both at tag `v1.0.0`), and none of the 9
  improvements have been coded yet — `FIRMWARE_IMPROVEMENTS.md` is still
  just a proposal. That means **we are at the best possible moment to set
  up a sane structure**, before local and upstream code diverge.
- Upstream is a small, active hobbyist project. It's reasonable to expect
  occasional upstream firmware fixes/releases in the future. We want a
  repeatable, low-ceremony process (PowerShell + Arduino IDE, no CI
  server) for absorbing those without re-doing all our local work by hand.

---

## 2. Decision 1 — Git workflow

**Options considered:** patch-queue (`git format-patch`/`am`, quilt-style),
long-lived rebasing feature branch, git subtree/submodule vendoring.

**Recommendation: a single long-lived local branch
(`firmware/local-improvements`), periodically rebased onto
`upstream/main`, with a lightweight fallback of exporting the branch as a
numbered patch series (`git format-patch`) whenever we want an
easy-to-read, one-file-per-improvement record.**

Why:
- The firmware is one file with no build system — subtree/submodule
  vendoring adds real overhead (nested repo, sync commands) for no benefit
  when there's only one file that matters.
- A patch-queue is attractive for auditability, but `git rebase
  --onto upstream/main` gives the same "replay my commits on top of new
  upstream code" result with native tooling and better merge-conflict UX
  (`git mergetool`, `git rebase --continue`) than manually fixing `.patch`
  files.
- Because we will also restructure the file (see Decision 2) into a thin
  `.ino` + extracted headers, future upstream diffs will mostly land in
  the thin `.ino`/`SETTINGS` region, which keeps rebase conflicts small
  and localized.
- We keep the option to run `git format-patch main..firmware/local-improvements`
  at any time to produce a numbered patch series for the changelog or for
  sharing upstream as a PR — so we get the audit trail benefit without
  committing to quilt-style patch maintenance day to day.

### 2.1 One-time setup

```powershell
git fetch upstream
git checkout -b firmware/local-improvements upstream/main
```

Each of the 9 improvements becomes its own commit (or short commit
sequence) on this branch, applied **in the priority order already given
in `FIRMWARE_IMPROVEMENTS.md`**:

1. Button debounce
2. EEPROM persistence
3. LED feedback
4. Named constants
5. Calibration sanity check
6. Hardcoded array size fixes
7. Logic/hardware separation (`orbit_logic.h`, etc.)
8. Fixed-point math (only if profiling shows it's needed)
9. Chord membership simplification/documentation

Keeping each improvement as its own commit means any one of them can be
reverted, cherry-picked, or skipped independently — a maintainer who only
wants EEPROM persistence and LED feedback can cherry-pick just those two
commits onto a clean checkout.

### 2.2 Runbook — upstream released a firmware update

```powershell
git fetch upstream
git diff upstream/main main -- Firmware/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino
```

- If the diff is empty, nothing to do.
- If not empty, read the diff (or run `scripts/check-upstream-diff.ps1`,
  see Decision 3) to understand what changed, then:

```powershell
git checkout firmware/local-improvements
git rebase upstream/main
```

- Resolve any conflicts commit-by-commit (each commit is a single
  improvement, so conflicts are easy to attribute to a specific feature).
- Re-run the compile check + PC-side unit tests + manual hardware
  checklist (Decision 3) before merging/tagging a new release.

### 2.3 Runbook — adding a new local improvement

```powershell
git checkout firmware/local-improvements
git checkout -b firmware/local-improvements/<short-name>
# make the change, keep it scoped to one improvement
git commit -am "firmware: <short description>"
git checkout firmware/local-improvements
git merge --ff-only firmware/local-improvements/<short-name>
```

Optionally export the whole branch as a readable patch series for the
changelog:

```powershell
git format-patch upstream/main..firmware/local-improvements -o FirmwareUpdates/patches
```

---

## 3. Decision 2 — Source architecture

**Recommendation: keep a single `.ino` as the sketch entry point (Arduino
IDE compiles every `.ino`/`.h`/`.cpp` in the sketch folder together, so
this is fully supported), but split it into a thin `.ino` plus a small set
of focused headers.** This is the "hybrid" approach: extract testable pure
logic and self-contained hardware modules into headers, while leaving the
tunable `SETTINGS`/constants block in the `.ino` exactly where it is today
so `Configurator/tuning.html` and `ButtonRemapper` keep working unmodified
(both tools regex-match `const TYPE NAME = value;` lines directly in the
`.ino` file).

### 3.1 Target file layout

```
Firmware/Hackman3D_Orbit_Controller/
├── Hackman3D_Orbit_Controller.ino   # settings/constants block (unchanged,
│                                     #   stays patchable by the browser tools),
│                                     #   pin config, globals, setup(), loop() —
│                                     #   becomes mostly "glue" calling into headers
├── orbit_logic.h                    # pure math: smoothValue, applyGain,
│                                     #   applyResponseCurve, applyInputDeadzone,
│                                     #   applyOutputDeadzone, keepOnlyDominantAxis,
│                                     #   countPositive4/countNegative4
├── orbit_buttons.h                  # readButtonMask + debounce, chord/mode-switch
│                                     #   and slicer-mode combo detection state machines
├── orbit_eeprom.h                   # EEPROM load/save of currentSpeedMode /
│                                     #   slicerMouseModeEnabled (improvement #2)
├── orbit_leds.h                     # non-blocking TX/RX LED status feedback
│                                     #   (improvement #3)
├── orbit_slicer_hid.h               # SlicerMouseHID_ class + mouse/keyboard
│                                     #   report helpers (moved as-is, no logic change)
└── orbit_hid_descriptors.h          # the three PROGMEM HID report descriptor arrays
```

Why this split and not something more aggressive:
- `orbit_logic.h` directly matches improvement #9 in
  `FIRMWARE_IMPROVEMENTS.md` — it is explicitly requested there and is the
  highest-value split for enabling PC-side (`gcc`) unit tests, since it has
  zero Arduino/hardware dependencies.
- `orbit_buttons.h` isolates the hardest-to-reason-about code (chord
  detection, improvement #3's documentation target, improvement #1's
  debounce) into one place, so future upstream button-logic changes and
  our local chord/debounce changes collide in one small file instead of
  being scattered through `loop()`.
- `orbit_eeprom.h` and `orbit_leds.h` are **wholly new** local
  improvements (#2 and #5 in priority order) with no upstream equivalent
  today — putting them in their own headers means upstream updates to the
  `.ino` essentially never touch these files, so rebases affecting them
  will be rare-to-never.
- `orbit_slicer_hid.h` and `orbit_hid_descriptors.h` are moved verbatim
  (no behavior change) purely to shrink the `.ino` further; this is
  optional polish, not required for the 9 improvements, and can be done
  last or skipped if it's not worth the churn.
- The `SETTINGS`/`SENSITIVITY`/pin-config constants block, `setup()`, and
  `loop()` **stay in the `.ino`**. This is intentional: it's the part of
  the file most likely to receive small upstream tuning tweaks, it's the
  part the browser tools depend on, and after extraction it's small enough
  (~250–350 lines) to review and rebase by eye.

### 3.2 Migration plan (no behavior change)

1. Create the new header files, move the relevant functions/classes into
   them verbatim, add `#include "orbit_x.h"` lines near the top of the
   `.ino`.
2. Compile-check with `arduino-cli compile` (Decision 3) after every single
   file move — one file at a time, one commit at a time — to guarantee
   zero behavior change before any new improvement code is added.
3. Only once the split compiles identically do we start adding the actual
   9 improvements, each targeted at the header that now owns that code:

| # | Improvement | Target file |
|---|---|---|
| 1 | Button debounce | `orbit_buttons.h` |
| 2 | EEPROM persistence | `orbit_eeprom.h` (new) |
| 3 | LED feedback | `orbit_leds.h` (new) |
| 4 | Named constants | `.ino` (SETTINGS block) |
| 5 | Calibration sanity check | `.ino` (`calibrateCenter()`) or a new `orbit_calibration.h` if it grows |
| 6 | Hardcoded array sizes → `BUTTON_COUNT` | `.ino` (globals) + `orbit_buttons.h` |
| 7 | Logic/hardware separation | `orbit_logic.h` (this *is* the separation) |
| 8 | Fixed-point math (conditional) | `orbit_logic.h` |
| 9 | Chord membership documentation | `orbit_buttons.h` (comment block) |

---

## 4. Decision 3 — Build, validation, and release workflow

**Recommendation:** a small set of PowerShell scripts under
`FirmwareTuning/scripts/`, plus `arduino-cli` for compile-checking and
plain `gcc`/`g++` for PC-side unit tests of `orbit_logic.h`. Versioned
build output lands in `FirmwareUpdates/` with a changelog entry — nothing
requires a CI server.

### 4.1 Scripts

```
FirmwareTuning/
├── scripts/
│   ├── check-upstream-diff.ps1   # git fetch upstream; diff the .ino path;
│   │                              #   prints a clear "diverged / not diverged" report
│   ├── build-firmware.ps1        # arduino-cli compile --fqbn <pro-micro fqbn>
│   │                              #   against the sketch folder
│   └── run-logic-tests.ps1       # g++ orbit_logic.h + tests/*.cpp, run the binary
└── tests/
    ├── test_orbit_logic.cpp      # plain C++ unit tests, no Arduino headers
    └── test_main.cpp             # minimal test runner / main()
```

### 4.2 What the unit tests assert (plain English — implementation later)

- `smoothValue`: moving toward target changes by roughly `delta /
  SMOOTH_DIVISOR`; a non-zero delta smaller than the divisor still moves
  by at least 1 (never gets permanently stuck just short of target); zero
  delta returns the same value unchanged.
- `applyGain`: output scales linearly with the gain factor; gain of `1.0`
  is a no-op; negative input stays negative.
- `applyResponseCurve`: input below the output deadzone maps to exactly
  `0`; input at the maximum magnitude maps to `maxMagnitude * speedScale`;
  the sign of the output always matches the sign of the input; curve `1.0`
  behaves linearly, curve `>1.0` suppresses small inputs more than large
  ones.
- `applyInputDeadzone` / `applyOutputDeadzone`: any value with
  `abs(value) < threshold` becomes `0`; values at or above the threshold
  are unchanged.
- `keepOnlyDominantAxis`: given 6 values, only the single
  largest-magnitude axis survives, all others become `0`; ties resolve to
  the earliest axis in fixed order (documents current behavior).
- `countPositive4` / `countNegative4`: correctly count how many of 4
  values exceed / fall below a signed threshold, including boundary values
  exactly at the threshold (not counted, since the check is strict `>`/`<`).
- New for improvement #1 (debounce): a button mask that changes for less
  than `BUTTON_DEBOUNCE_MS` and then reverts is *not* reported as a change;
  a mask that stays changed for at least `BUTTON_DEBOUNCE_MS` is reported.
- New for improvement #4/#5 (calibration check): a center value inside
  `[CALIBRATION_MIN, CALIBRATION_MAX]` passes; a value outside it is
  flagged.

### 4.3 Maintainer sequence for a new release build

```powershell
# 1. Confirm upstream state
.\FirmwareTuning\scripts\check-upstream-diff.ps1

# 2. Rebase local improvements if upstream moved (Decision 1, §2.2)
git checkout firmware/local-improvements
git rebase upstream/main

# 3. Run PC-side logic unit tests
.\FirmwareTuning\scripts\run-logic-tests.ps1

# 4. Compile-check against the real target board
.\FirmwareTuning\scripts\build-firmware.ps1

# 5. Manual hardware checklist (can't be automated):
#    - startup calibration still works, sanity check flags bad centers
#    - all 3 buttons: individual press, mode-switch chord, slicer-mode chord
#    - EEPROM: change mode, power-cycle, confirm it restored
#    - LED feedback matches the table in FIRMWARE_IMPROVEMENTS.md
#    - Configurator/tuning.html and ButtonRemapper still open/save the .ino correctly

# 6. Tag + record the release
#    - copy the built/tested sketch folder into FirmwareUpdates/vX.Y.Z/
#    - add a CHANGELOG.md entry
#    - git tag firmware-vX.Y.Z
```

### 4.4 Keeping the browser tools working

Because `Configurator/tuning.html` and `ButtonRemapper` parse
`const TYPE NAME = value;` lines out of the `.ino` by name, any change
under Decision 2/3 must preserve, for every constant those tools reference:
- the exact constant name,
- that it remains a top-level `const` declaration directly in the `.ino`
  (not moved into a header),
- its declaration stays on a single line in the recognizable
  `const TYPE NAME = value;` shape.

Whenever a new named constant is introduced by improvement #4 or by the
remapper-support changes in `FIRMWARE_REMAPPER_SUPPORT.md`, add it to the
tools' constant lists (`SHORTCUT_CONSTANTS` in the remapper, the
parameter list in `tuning.html`) in the same commit, and re-open both
tools against the updated `.ino` as a manual regression check.

---

## 5. Summary

| Decision | Choice |
|---|---|
| Git workflow | Long-lived branch `firmware/local-improvements`, rebased on `upstream/main`; `git format-patch` export as an optional audit trail |
| Source architecture | Thin `.ino` (settings + `setup()`/`loop()`) + `orbit_logic.h`, `orbit_buttons.h`, `orbit_eeprom.h`, `orbit_leds.h`, (optional) `orbit_slicer_hid.h` / `orbit_hid_descriptors.h` |
| Validation | `arduino-cli compile` + plain `gcc`/`g++` unit tests on `orbit_logic.h` + manual hardware checklist |
| Release output | Versioned copy under `FirmwareUpdates/vX.Y.Z/` + `CHANGELOG.md` entry + git tag |
| Compatibility constraint | All browser-tool-patchable constants stay as top-level `const` lines in the `.ino` |
