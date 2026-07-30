# QMK Port — HackMan3D Orbit Controller

This folder is the **documentation home** for porting the Orbit Controller firmware from
Arduino to QMK.

- **Docs live here:** `qmk/` (this folder, in the `HackMan3D-Orbit-Controller` repo)
- **Code lives there:** `d:\GitHub2\qmk_firmware\keyboards\hackman3d\`
- **Branch:** `feature/qmk-port`

## Goal

Port `FirmwareUpdates/v1.1.0/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino`
(+ its 4 headers) to QMK **without losing a single feature**, and add two things the
Arduino firmware cannot do:

1. **Live keymap editing** — remap the 3 buttons (and their long-press actions) from a GUI,
   no reflash.
2. **Live tuning** — edit deadzones, per-axis gains, speed modes, response curve, smoothing,
   rotation priority, Z-detection thresholds, slicer-mouse parameters and all timings from a
   GUI, applied instantly, saved to EEPROM.

Both are delivered via **VIA v3 custom menus** (`VIA_ENABLE`), which is the same mechanism
used by the reference project [`plodah/ploopy_viamenus`](https://github.com/plodah/ploopy_viamenus)
— an ATmega32U4 QMK build exposing ~90 live-editable parameters. That project proves the
approach fits on our MCU.

## Document index

| Doc | Contents |
|---|---|
| [`01_FIRMWARE_INVENTORY.md`](01_FIRMWARE_INVENTORY.md) | Source of truth: every parameter, every behavior, the HID descriptors, the fixed-point math, the chord state machine. **Read before writing any code.** |
| [`02_QMK_ARCHITECTURE.md`](02_QMK_ARCHITECTURE.md) | Target file layout, feature-by-feature mapping, keyboard.json/rules.mk/config.h, execution model. |
| [`03_HID_DESCRIPTOR_FORK.md`](03_HID_DESCRIPTOR_FORK.md) | The 3Dconnexion Multi-axis Controller descriptor and the exact QMK core patch required. |
| [`04_VIA_CUSTOM_MENUS.md`](04_VIA_CUSTOM_MENUS.md) | Live-editing design: channel/value-ID map, the VIA JSON, the C handler, EEPROM layout. |
| [`05_SIZE_BUDGET.md`](05_SIZE_BUDGET.md) | Measured flash/RAM/EEPROM budget on the 28,672-byte 32U4 target, and the trim list. |
| [`06_TASKLIST.md`](06_TASKLIST.md) | Phased, checkbox task list — the working plan. |
| [`links.md`](links.md) | Reference links. |

## Current status

Scaffold is in place and **both builds compile**:

| Build | Flash | Budget |
|---|---|---|
| `hackman3d/orbit_controller:default` | 10,574 B (36 %) | 28,672 B |
| `hackman3d/orbit_controller:viam` | 11,920 B (41 %) | 28,672 B |

VIA costs only **1,346 bytes** here — the original feasibility study's fear that VIA "likely
does not fit" was wrong by ~4×. With ~16.7 KB free in the VIA build and the application
estimated at ~6.5 KB, there is ample headroom. Details in `05_SIZE_BUDGET.md`.

`qmk lint` passes for both keymaps. Next up is Phase 0 (golden-reference test harness) and
Phase 1 (the QMK core patch). See `06_TASKLIST.md`.

## Building

From the root of the QMK tree that contains `keyboards/hackman3d/`:

```bash
make hackman3d/orbit_controller:default   # 6DOF only
make hackman3d/orbit_controller:viam      # + VIA live keymap & tuning
```

Or via the CLI (`-j 0` = unlimited parallel jobs, `SKIP_GIT=true` skips the submodule check):

```bash
SKIP_GIT=true qmk compile -j 0 -kb hackman3d/orbit_controller -km viam
```

Two things that will bite you:

1. **The VIA keymap is called `viam`, not `via`.** QMK master gitignores
   `/keyboards/**/keymaps/via/*` and `qmk lint` fails with *"The keymap via should not
   exist!"* — VIA keymaps were deprecated from the main repo. `viam` (the `ploopy_viamenus`
   convention) still sets `VIA_ENABLE = yes` and builds byte-identical firmware.

2. **`qmk compile` uses `user.qmk_home`, and `QMK_HOME` does not override it.** If you get
   `invalid keyboard_folder_or_all value: 'hackman3d/orbit_controller'`, the CLI is looking at
   a different QMK checkout:

   ```bash
   qmk config user.qmk_home                       # where is it looking?
   qmk config user.qmk_home=/path/to/qmk_firmware # repoint it
   ```

   Exporting `QMK_HOME=...` has **no effect** — the CLI reads its config file. Using `make`
   from the correct tree avoids the issue entirely.

## Prior work

`FirmwareTuning/QMK_PORT_FEASIBILITY.md` was the initial feasibility study. It concluded
"yes, via a small core fork." That conclusion stands; these documents supersede it with
measured numbers, a concrete VIA design, and an actionable plan.

Key deltas from the original study:

- The response curve is **already** a fixed-point LUT in v1.1.0 (`orbit_logic.h`) — the
  "convert `pow()` to a LUT to save 2 KB" recommendation is already done, so the flash
  picture is better than the study assumed.
- VIA on 32U4 alongside joystick + pointing device is **measured**, not estimated — see
  `05_SIZE_BUDGET.md`.
- The v1.1.0 firmware is *five* files, not one, and has EEPROM persistence + LED feedback +
  a debounce layer the original study did not account for.
