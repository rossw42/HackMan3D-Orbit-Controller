# QMK Port Feasibility Study — HackMan3D Orbit Controller

**Question:** Can `Firmware/Hackman3D_Orbit_Controller/Hackman3D_Orbit_Controller.ino` be ported to QMK firmware **on the same controller** (Arduino Pro Micro, ATmega32U4)?

**Short answer: YES — it is technically possible, but with one major caveat.**

A *stock* (unmodified) QMK build can replicate every feature of the current firmware **except** 3Dconnexion/3DxWare driver recognition. Full SpaceMouse compatibility (Fusion 360, SolidWorks, etc. via the 3DxWare driver) requires a **small fork of the QMK core** (~3 files) plus a VID/PID spoof — both of which are proven, achievable modifications. The tightest real-world constraint is not capability, it is **flash space** (28 KB usable on the 32U4), which limits how much of QMK's configurability (notably VIA) can ride along.

This document details the full analysis: hardware fit, feature-by-feature mapping, USB/HID constraints, flash budget, three concrete porting paths, and a recommendation.

---

## 1. Hardware Inventory (what must be supported)

From the current sketch and project docs:

| Resource | Current firmware | Notes |
|---|---|---|
| MCU / Board | ATmega32U4, Arduino Pro Micro (5 V/16 MHz, USB-C) | Caterina bootloader (4 KB) |
| Analog inputs | 8 channels from 4× JH16 Hall-effect joysticks: `A1, A0, A3, A2, A7, A6, A9, A8` | 10-bit ADC reads |
| Buttons | 3, on digital pins `2, 3, 7`, active-low w/ internal pullups | Chorded: speed-mode cycle, slicer-mode toggle, long-press actions |
| USB HID | Custom composite device (see §3) | Multi-axis Controller + Mouse + Keyboard |

### 1.1 Does QMK run on this MCU? — Yes, first-class support

- The ATmega32U4 is QMK's most common legacy platform (LUFA USB stack, `PLATFORM = AVR`).
- `platforms/avr/bootloader.mk` explicitly supports `BOOTLOADER = caterina` ("Pro Micro Sparkfun/generic") with `BOOTLOADER_SIZE = 4096`. Flashing works through the standard Caterina 1200-baud-touch / double-tap-reset avrdude flow — **no bootloader change needed**; you can flash back and forth between Arduino and QMK freely.

### 1.2 Can QMK read all 8 analog channels? — Yes

QMK's AVR analog driver (`platforms/avr/drivers/analog.c`, `analogReadPin()`) supports the full 32U4 ADC mux, including the "extended" channels used by the Pro Micro's A6–A9:

| Arduino pin | 32U4 port pin | ADC channel | QMK pin name | Supported by `analogReadPin()`? |
|---|---|---|---|---|
| A0 | PF7 | ADC7 | `F7` | ✅ |
| A1 | PF6 | ADC6 | `F6` | ✅ |
| A2 | PF5 | ADC5 | `F5` | ✅ |
| A3 | PF4 | ADC4 | `F4` | ✅ |
| A6 | PD4 | ADC8 | `D4` | ✅ |
| A7 | PD7 | ADC10 | `D7` | ✅ |
| A8 | PB4 | ADC11 | `B4` | ✅ |
| A9 | PB5 | ADC12 | `B5` | ✅ |

Resolution is 10-bit, identical to `analogRead()` in the current sketch, so **all calibration constants, deadzones and gain math carry over unchanged**.

### 1.3 Buttons — trivially yes

Pins `D2 (PD1)`, `D3 (PD0)`, `D7 (PE6)` map to a 1×3 direct-pin "matrix" (`DIRECT_PINS`) with internal pullups — standard QMK.

---

## 2. Feature-by-Feature Port Mapping

Every behavior in the 1,757-line sketch maps to a QMK mechanism:

| Sketch feature | QMK equivalent | Effort |
|---|---|---|
| `analogRead()` of 8 channels | `analogReadPin()` (ADC driver, `ANALOG_DRIVER_REQUIRED = yes`) | Direct port |
| Startup center calibration (100 samples) | `keyboard_post_init_kb()` | Direct port |
| Deadzones, gains, response curve (`pow()`), smoothing | Plain C in `housekeeping_task_kb()`/custom code; `<math.h>` available (float `pow()` costs ~2 KB flash on AVR — see §5) | Direct port |
| Z push/pull & Z-twist consensus detection | Plain C, unchanged | Direct port |
| Rotation-priority / dominant-axis filter | Plain C, unchanged | Direct port |
| 6-axis output (TX/TY/TZ/RX/RY/RZ) | **Joystick feature**: `JOYSTICK_AXIS_COUNT = 6` (max supported = 6, exactly X, Y, Z, Rx, Ry, Rz — Generic Desktop usages 0x30–0x35), `JOYSTICK_AXIS_RESOLUTION = 16` (8–16 bit supported), driven via `joystick_set_axis()` | Direct port ⚠ *but see §3 for driver-recognition caveat* |
| 3 HID buttons | Joystick buttons (`JOYSTICK_BUTTON_COUNT`) or keycodes `JS_0…` | Direct port |
| Speed-mode chord (3 buttons, debounce, suppression) | Custom `process_record_kb()` state machine, or QMK combos | Direct port |
| Slicer mouse mode (relative mouse drag + wheel emulation) | **Pointing Device** feature with `POINTING_DEVICE_DRIVER = custom`; build the `report_mouse_t` (x, y, wheel `v`, buttons) in `pointing_device_task()` | Direct port |
| Slicer keyboard shortcuts (Cmd+0, N, L, Tab, Cmd+Shift+G…) | `tap_code16(LGUI(KC_0))` etc. — first-class QMK | Direct port, *better* than sketch |
| Long-press vs short-press button actions | Tap-hold (`LT`/custom) or timers — first-class QMK | Direct port, *better* |
| Per-mode toggle of mouse vs 6-axis output | Custom mode flag or QMK layers | Direct port |
| Debug serial plotter output | QMK Console (`CONSOLE_ENABLE`) — costs 1 endpoint + flash | Optional |

**Conclusion of this section:** 100 % of the *behavior* is portable. The open question is purely at the **USB descriptor level** — what the device *looks like* to the host.

---

## 3. The Critical Issue: HID Descriptor & 3DxWare Compatibility

### 3.1 What the current firmware does

The sketch is a **SpaceMouse impersonator**. Its primary HID interface declares:

```
Usage Page (Generic Desktop 0x01)
Usage (Multi-axis Controller 0x08)      ← the key usage 3DxWare looks for
  Report ID 1: Translation X/Y/Z  (3 × int16)
  Report ID 2: Rotation  Rx/Ry/Rz (3 × int16)
  Report ID 3: 32 buttons
```

This is the classic 3Dconnexion SpaceNavigator/SpaceMouse report layout. Combined with the **NavCore 3D Controller board package** (which sets the USB VID/PID — the ecosystem standard is VID `0x256F` / PID `0xC631`, "SpaceMouse Pro Wireless (cabled)"), the 3DxWare driver and CAD applications treat it as a genuine SpaceMouse. This is exactly how AndunHH/spacemouse and the TeachingTech open-source SpaceMouse achieve compatibility — reverse-engineered protocol + spoofed IDs.

Additionally the sketch plugs a **second HID interface** (PluggableUSB) carrying a boot-protocol relative mouse (Report ID 1) and a keyboard (Report ID 2) for slicer mode.

### 3.2 What stock QMK can and cannot do

| Requirement | Stock QMK | Verdict |
|---|---|---|
| 6 × 16-bit axes X/Y/Z/Rx/Ry/Rz in one HID report | ✅ Joystick feature, `JOYSTICK_AXIS_COUNT=6`, `JOYSTICK_AXIS_RESOLUTION=16` | OK |
| Top-level usage **Multi-axis Controller (0x08)** | ❌ Hardcoded to **Usage Joystick (0x04)** in `tmk_core/protocol/usb_descriptor.c` (`JoystickReport`). Not configurable via `config.h`. | **Blocker for 3DxWare** |
| Split Report IDs 1 (translation) / 2 (rotation) / 3 (buttons) | ❌ QMK sends one combined joystick report | **Blocker for 3DxWare** |
| Custom VID/PID (`0x256F`/`0xC631`) | ✅ Freely settable in `keyboard.json` (`usb.vid`, `usb.pid`) | OK |
| Custom manufacturer/product strings ("3Dconnexion") | ✅ `keyboard.json` | OK |
| User-defined custom HID interface without touching core | ❌ No hook exists. All descriptors are hardcoded behind feature `#ifdef`s. | Confirmed |
| Raw HID as a workaround | ❌ `RAW_USAGE_PAGE`/`RAW_USAGE_ID` *are* configurable (could claim 0x01/0x08), but the report body remains flat 32-byte vendor arrays with no axis usages and no Report IDs. 3DxWare parses descriptor *structure*, so it will not bind. | Not viable |

### 3.3 What the host side accepts

- **3DxWare (Windows/macOS):** binds by VID/PID (0x256F family) *and* expects the multi-axis descriptor layout. A generic joystick — even with 6 perfectly-named axes — is **not** picked up. This is why simply enabling QMK's joystick feature is not enough for CAD apps that rely on the 3Dconnexion driver.
- **spacenavd (Linux, free driver):** detects 3Dconnexion USB devices by ID and can be configured for additional event devices; a spoofed-ID QMK device with the correct reports works the same as the Arduino version. A plain QMK joystick can also be fed to some applications via `libspnav`'s X11/uinput paths with manual configuration, but it is not plug-and-play.
- **Applications with native generic-joystick 6DOF support** (some viewers, Blender via addons, games): stock QMK's 6-axis joystick works out of the box.

### 3.4 So is the port possible? — Yes, via a small core fork

Because QMK is GPL open source, the joystick interface can be converted into a Multi-axis Controller. This has been done before by hobbyist QMK-SpaceMouse forks. The change is well-bounded:

**Files to modify (QMK fork):**

1. **`tmk_core/protocol/usb_descriptor.c`**
   - Replace the `JoystickReport` HID report descriptor: change `HID_RI_USAGE(8, 0x04)` (Joystick) → `HID_RI_USAGE(8, 0x08)` (Multi-axis Controller); restructure into three collections with Report IDs 1/2/3 exactly as in the sketch's `hidReportDescriptor[]` (which can be transliterated almost byte-for-byte into LUFA `HID_RI_*` macros).
2. **`tmk_core/protocol/report.h`**
   - Redefine `report_joystick_t` as the 3-report union (`{uint8_t report_id; int16_t axes[3];}` × 2 + button report) or add a dedicated struct.
3. **`quantum/joystick.c` / `tmk_core/protocol/lufa/lufa.c` (`send_joystick`)**
   - Split the single send into two sequential endpoint writes (Report 1 then Report 2), mirroring `sendCommand()` in the sketch, plus Report 3 on button change. The sketch's own `HID().SendReport(1/2/3, …)` pattern maps 1:1 onto `usb_endpoint_interrupt_send()` calls.

Estimated scope: **< 200 changed lines**, one-time, maintained as a small patch on top of a QMK release tag. Difficulty: moderate — the descriptor already exists verbatim in the `.ino`, so this is a translation exercise, not a design exercise.

---

## 4. USB Endpoint Budget (ATmega32U4) — Fits

The 32U4 has 7 hardware endpoints (EP0 control + **6 usable**). QMK enforces this at compile time (`usb_descriptor.h`: `#error There are not enough available endpoints`). Budget for the full-featured port:

| Interface | Endpoints | Needed? |
|---|---|---|
| Keyboard + Mouse + Extrakeys via `KEYBOARD_SHARED_EP`/`MOUSE_SHARED_EP` | 1 | ✅ (slicer keyboard shortcuts + slicer relative mouse) |
| Joystick → Multi-axis Controller | 1 | ✅ (6-axis reports) |
| Raw HID (VIA) | 2 (IN + OUT) | Optional |
| Console (debug) | 1 | Optional (mutually budgetable) |
| **Total (core port)** | **2 of 6** | ✅ Comfortable |
| **Total (with VIA + console)** | **5 of 6** | ✅ Still fits |

**Verdict: endpoints are not a blocker.** The current sketch itself uses 2 IN endpoints; QMK's shared-endpoint options keep the port well within limits.

---

## 5. Flash & RAM Budget — The Real Constraint

- Usable flash: 32 KB − 4 KB Caterina = **28,672 bytes**.
- Current Arduino sketch: comparable footprint headroom (Arduino core + PluggableUSB + float math).
- QMK baseline on 32U4: roughly 18–23 KB depending on features.

Estimated QMK build sizes for this port:

| Configuration | Estimated flash | Fits? |
|---|---|---|
| Core port: joystick(6×16-bit) + pointing device (custom) + basic keycodes + custom axis math incl. float `pow()` (~2 KB) | ~22–25 KB | ✅ Yes |
| + Console debugging | +1.5–2 KB | ⚠ Tight — enable only for debug builds |
| + VIA (Raw HID + dynamic keymaps) | +4–6 KB | ⚠ **Likely does not fit** alongside everything else; would require trimming (e.g., LTO — mandatory; drop console; convert `pow()` response curve to a lookup table, saving ~2 KB and CPU) |
| + VIA + Console + everything | >28 KB | ❌ No |

Mitigations that are standard practice on 32U4:
- `LTO_ENABLE = yes` (saves 1.5–3 KB).
- Replace the float `pow()` response curve with a 32-entry `PROGMEM` lookup table + interpolation (saves flash *and* loop time).
- Disable magic keycodes, space cadet, grave-esc, etc. (`rules.mk` trims).

**RAM (2.5 KB):** not a concern; the port's state is a few hundred bytes, and QMK's USB buffers fit comfortably.

**Verdict:** the core port fits with room to spare. The *aspirational* payoff of QMK — **VIA graphical remapping** — is possible only with aggressive trimming, and may ultimately be the deciding factor for whether QMK is worth it on *this* MCU (vs. an RP2040 swap, see §8).

---

## 6. What Would Be Gained / Lost

### Gained by porting to QMK
- **Keymap/layer infrastructure**: remap the 3 buttons, tap-hold, combos, layers — replaces ~400 lines of hand-rolled chord/debounce code in the sketch with battle-tested QMK primitives.
- **EEPROM-backed settings** (`eeconfig`): persist speed mode, slicer mode, axis inversion across power cycles (the sketch currently loses mode on unplug).
- **VIA/remapper path** (flash permitting) — directly relevant to `FIRMWARE_REMAPPER_SUPPORT.md` goals; the existing ButtonRemapper/Configurator plans could be replaced by VIA or QMK's Raw HID protocol.
- Community tooling: QMK CLI, CI builds, `qmk flash`, standardized configuration.

### Lost / at risk
- **3DxWare compatibility requires the core fork** (§3.4). A fork must be rebased when updating QMK (small, but a permanent maintenance cost).
- **Loop timing**: the sketch free-runs its analog pipeline every loop; QMK's scan loop plus USB housekeeping adds overhead on a 16 MHz AVR. 8 ADC reads (~112 µs each = ~0.9 ms) + float math per cycle is fine, but the float `pow()` calls (6× per loop) should become a LUT to keep the report rate at the SpaceMouse-typical ~125 Hz.
- **Simplicity**: the current single-`.ino` firmware is trivially editable by end users in Arduino IDE. QMK demands a toolchain (`qmk setup`), which raises the contribution bar for this project's audience.

---

## 7. Concrete Porting Paths

### Path A — Stock QMK, generic 6-axis joystick (no core changes)
- `JOYSTICK_ENABLE = yes`, `JOYSTICK_AXIS_COUNT = 6`, `JOYSTICK_AXIS_RESOLUTION = 16`, `JOYSTICK_DRIVER = digital` with `joystick_set_axis()` fed from custom `housekeeping_task_kb()` running the ported axis pipeline; `POINTING_DEVICE_ENABLE = yes` + `POINTING_DEVICE_DRIVER = custom` for slicer mode; `DIRECT_PINS` for buttons.
- **Result:** fully working 6DOF *joystick*. Works with Blender addons, games, spacenavd-with-config. **Not recognized by 3DxWare / Fusion 360 / SolidWorks.**
- Effort: ~2–4 days. Risk: low.

### Path B — QMK fork with Multi-axis Controller descriptor (recommended for full parity)
- Path A **plus** the 3-file core patch from §3.4, VID/PID `0x256F:0xC631`, product string "SpaceMouse Pro Wireless (cabled)".
- **Result:** full parity with the current firmware, including 3DxWare/CAD recognition, plus QMK's keymap/EEPROM/tooling benefits.
- Effort: ~1–2 weeks including host-driver validation on Windows/macOS/Linux. Risk: moderate (descriptor byte-accuracy matters; test with USB descriptor dump tools + 3DxWare on each OS).
- Maintenance: keep the patch as a branch off a QMK release tag; rebase ~yearly.

### Path C — Hybrid (pragmatic alternative)
- Keep the proven Arduino HID core (descriptor + `sendCommand()`), but adopt the *ideas* QMK would bring: EEPROM-persisted config + a serial/Raw-HID config protocol (already sketched in `FIRMWARE_REMAPPER_SUPPORT.md`). No QMK at all.
- **Result:** zero compatibility risk, remapping achieved, no toolchain change for users. Foregoes QMK ecosystem.
- Effort: comparable to Path A.

---

## 8. Side Note: The MCU Question

If QMK is desired primarily for **VIA remapping**, note that the ATmega32U4 is the limiting factor, not QMK. A drop-in **RP2040 Pro Micro clone** (SparkFun Pro Micro RP2040, ~same footprint) would give QMK 16 MB flash, 264 KB RAM, more endpoints, native 12-bit ADC (⚠ only 4 ADC channels — would need an external ADC or analog mux for 8 channels, which *is* a hardware change), and trivially fits VIA + console + everything. Since the task constraint is "same controller," this is out of scope, but worth recording: **on the 32U4, choose between full VIA and comfortable margins; on RP2040 you'd have both — at the cost of an analog mux.**

---

## 9. Verdict & Recommendation

| Question | Answer |
|---|---|
| Can the firmware be ported to QMK on the same ATmega32U4 Pro Micro? | **Yes.** |
| With zero QMK modifications? | Yes, but only as a **generic 6-axis joystick** (Path A) — loses 3DxWare/CAD driver support. |
| With full feature parity incl. SpaceMouse emulation? | **Yes, via a small (~3-file, <200-line) QMK core fork** (Path B) + VID/PID spoof — a proven, well-bounded modification. |
| Hard blockers found? | **None.** MCU, bootloader, all 8 ADC channels, endpoints, and every behavioral feature check out. |
| Soft constraints | Flash budget: core port fits (~22–25 KB / 28 KB); adding VIA requires trimming (LTO, LUT response curve, no console) and may not fit; float `pow()` should become a lookup table regardless. |
| Recommendation | If QMK's ecosystem (layers, EEPROM config, potential VIA) justifies a maintained fork: **Path B**. If the only goal is button remapping: **Path C** (extend the current Arduino firmware) is lower-risk and matches the plans already in `FIRMWARE_REMAPPER_SUPPORT.md`. |

---

## Appendix A — Proposed QMK Keyboard Skeleton (Path B)

```
keyboards/hackman3d/orbit_controller/
├── keyboard.json          # vid: 0x256F, pid: 0xC631, mcu: atmega32u4, bootloader: caterina
├── config.h               # JOYSTICK_AXIS_COUNT 6, JOYSTICK_AXIS_RESOLUTION 16,
│                          # DIRECT_PINS {{D1, D0, E6}}, analog pin table
├── rules.mk               # JOYSTICK_ENABLE=yes (digital driver),
│                          # POINTING_DEVICE_ENABLE=yes, POINTING_DEVICE_DRIVER=custom,
│                          # ANALOG_DRIVER_REQUIRED=yes, LTO_ENABLE=yes
├── orbit_controller.c     # calibration, deadzones, gains, LUT response curve,
│                          # Z-consensus detection, smoothing, mode state machine,
│                          # housekeeping_task_kb() → joystick_set_axis()/multiaxis send,
│                          # pointing_device_task() → slicer mouse mode
└── keymaps/default/keymap.c   # 3-button layout, chords/combos, slicer shortcuts
```

Core patch (separate branch of qmk_firmware):
- `tmk_core/protocol/usb_descriptor.c` — Multi-axis Controller descriptor (Report IDs 1/2/3)
- `tmk_core/protocol/report.h` — 3-report joystick structs
- `tmk_core/protocol/lufa/lufa.c` — split-report `send_joystick`

## Appendix B — Key Facts Reference

- QMK joystick: max **6 axes** (exactly X/Y/Z/Rx/Ry/Rz), 8–16-bit resolution, `joystick_set_axis()` API, usage hardcoded to Joystick (0x04) in `tmk_core/protocol/usb_descriptor.c`.
- 32U4 endpoints: 6 usable; QMK compile-time checked; shared-EP options (`KEYBOARD_SHARED_EP`) free up slots.
- QMK AVR ADC: 10-bit, supports ADC0–ADC13 mux incl. extended channels on `D4/D7/B4/B5` — covers all four JH16 joysticks.
- DIY SpaceMouse ecosystem (AndunHH/spacemouse, TeachingTech): VID `0x256F` / PID `0xC631` + Multi-axis Controller usage + Report IDs 1 (T), 2 (R), 3 (buttons) [, 4 LED] at ~125 Hz is the de-facto recipe for 3DxWare recognition; QMK's stock descriptors do not match it, hence the fork requirement.
- Raw HID cannot substitute: usage page/ID are configurable but the report structure is fixed vendor-style; 3DxWare parses descriptor structure.

*Document generated as part of the FirmwareTuning research series.*