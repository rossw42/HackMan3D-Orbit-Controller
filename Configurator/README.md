# Hackman3D Orbit Controller — Configurator

A browser-based tool for tuning and button remapping the Hackman3D Orbit Controller firmware.  
No installation, no server, and no extra software required.

---

## Requirements

| Requirement | Detail |
|---|---|
| **Browser** | Google Chrome or Microsoft Edge (any recent version) |
| **Firmware** | v1.0.0 shipped firmware (`Hackman3D_Orbit_Controller.ino`) |
| **File access** | The browser must be allowed to read and write local files |

> **Firefox and Safari are not supported.** The configurator uses the [File System Access API](https://developer.mozilla.org/en-US/docs/Web/API/File_System_Access_API), which is only available in Chromium-based browsers.

---

## Opening the configurator

1. Open `HackMan3D_Orbit_Controller-configurator.html` directly in Chrome or Edge.  
   You can double-click the file or drag it into an open browser window.
2. Click **Open sketch folder**.
3. Select the folder that contains `Hackman3D_Orbit_Controller.ino`  
   (for example, `Firmware/Hackman3D_Orbit_Controller/`).
4. The browser will ask for permission to read and write files in that folder — click **Allow**.
5. The editor will open automatically once the firmware file is found.

The firmware version badge in the top bar confirms which file was loaded.

---

## Tabs

### ⚙ Tuning

Adjust how the controller feels — dead zones, smoothing, speed, sensitivity, and slicer mouse mode.  
All settings map directly to `const` values near the top of the `.ino` file.

Use the **Expand All / Collapse All** button to show or hide all sections at once.

| Section | What it controls |
|---|---|
| **Dead zones** | Electrical noise filter (input) and drift prevention (output) |
| **Smoothing & calibration** | Signal smoothing and startup calibration sample count |
| **Speed & response curve** | Global speed cap, response curve shape, and default speed mode |
| **Translation sensitivity** | Per-axis gain for left/right, forward/backward, and up/down |
| **Rotation sensitivity** | Per-axis gain for X, Y, and Z rotation |
| **Axis behavior** | Rotation priority and optional dominant-axis filter |
| **Speed mode profiles** | Per-mode scale and curve for Slow, Normal, and Fast |
| **Speed mode button shortcut** | How many buttons trigger a speed mode change and timing windows |
| **Slicer mouse mode** | Mouse emulation settings for slicers that do not support SpaceMouse HID |
| **Serial debug output** | Enable/disable debug printing and output interval |

**Tip:** Change one setting at a time, save, re-flash, and test before adjusting the next.  
See [TUNING_GUIDE.md](../TUNING_GUIDE.md) for a full parameter reference with recommended ranges.

---

### ⌨ Button Remapping

Assign a keyboard shortcut to each of the three buttons for use in **slicer mouse mode**.

Each button has a **short press** and a **long press** slot.  
Each slot has a **Modifier** dropdown and a **Key** dropdown.  
A live preview shows the resulting key combination.

> Some slots have a fixed modifier (shown as **None (fixed)**). These are shared constants in the firmware — changing the modifier for one slot changes it for all slots that use the same constant.

#### Default slicer shortcuts

| Button | Short press | Long press |
|---|---|---|
| 1 | Tab | ⌘ Cmd + Shift + G |
| 2 | N | L |
| 3 | ⌘ Cmd + 0 | A |

The `⌘ Cmd` shortcuts are macOS defaults. If you are on Windows or Linux, change the modifier to **Ctrl** where appropriate.

---

## Available keys

The Key dropdown includes the full standard HID keyboard set:

| Group | Keys |
|---|---|
| Letters | A – Z |
| Numbers | 0 – 9 |
| Punctuation | `-` `=` `[` `]` `\` `;` `'` `` ` `` `,` `.` `/` |
| Control keys | Enter, Escape, Backspace, Tab, Space, Caps Lock |
| Function row | F1 – F12 |
| Special | Print Screen, Scroll Lock, Pause, Insert |
| Navigation | Home, End, Page Up, Page Down, Delete |
| Arrow keys | Up, Down, Left, Right |

---

## Available modifiers

| Modifier | Description |
|---|---|
| None | No modifier |
| Ctrl | Control |
| Shift | Shift |
| Ctrl + Shift | |
| Alt | Alt / Option |
| Ctrl + Alt | |
| Shift + Alt | |
| Ctrl + Shift + Alt | |
| ⌘ Cmd | Command (macOS) |
| ⌘ Cmd + Ctrl | |
| ⌘ Cmd + Shift | |
| ⌘ Cmd + Ctrl + Shift | |
| ⌘ Cmd + Alt | |
| ⌘ Cmd + Shift + Alt | |

---

## Saving

When you are ready to apply your changes:

1. (Optional) Check **Save backup of original alongside it** — this writes a timestamped `.ino` copy in the same folder before overwriting.
2. Click **Save all changes to .ino**.

The file is written back in-place. The status bar confirms the save or reports any error.

After saving, re-flash the firmware using **Arduino IDE** for the changes to take effect.

> The configurator writes **only** the values it knows about. All other code in the `.ino` file is left untouched.

### Discarding changes

- **Discard tuning changes** — reverts the Tuning tab to the values that were in the file when it was opened.
- **Discard remap changes** — reverts the Button Remapping tab.
- **Reset tuning to defaults** — resets all tuning values to factory defaults. Click **Save all changes** afterwards to write them.

---

## Workflow summary

```
Open sketch folder → edit settings → Save all changes → re-flash with Arduino IDE → test
```

One setting at a time is strongly recommended. Flashing takes only a few seconds.

---

## Files in this folder

| File | Description |
|---|---|
| `HackMan3D_Orbit_Controller-configurator.html` | Main configurator — open this in Chrome or Edge |
| `README.md` | This file |
