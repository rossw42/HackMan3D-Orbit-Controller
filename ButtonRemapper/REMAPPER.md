# Hackman3D Button Remapper

A browser-based configurator for remapping the slicer layer button shortcuts on the Hackman3D Orbit Controller. No server, no installation — just open the HTML file and go.

---

## Requirements

- Chrome or Edge (the File System Access API is not supported in Safari or Firefox)
- The `Hackman3D_Orbit_Controller.ino` file somewhere on your machine

---

## Usage

1. Open `remap.html` in Chrome or Edge
2. Click **Open .ino file** and pick `Hackman3D_Orbit_Controller.ino`
3. Make your changes
4. Click **Save to .ino**

The file is written in place. A timestamped backup is automatically downloaded to your Downloads folder before every save.

---

## What you can change

The remapper edits the **slicer layer** only — the shortcuts that fire when the device is in slicer mouse mode.

Each of the 3 buttons has two configurable slots:

| Slot | Trigger |
|------|---------|
| Short press | Tap and release |
| Long press | Hold ≥ 650 ms |

For each slot you can set:
- **Key** — any letter, number, function key, navigation key, or special key
- **Modifier** — None, Ctrl, Shift, Alt, Cmd, or combinations (shown where the firmware supports an independent modifier for that slot)

The header of each button card shows the current short press and long press assignments at a glance, and updates live as you make changes.

---

## Modifier availability

Not every slot has an independent modifier — some share `SLICER_SHORTCUT_MODIFIER_PRIMARY` with another button's slot. Where a modifier dropdown is shown, it is fully editable. Where no modifier dropdown appears, the modifier is hardcoded in the firmware and requires a code change to alter.

---

## Backup

Every save downloads a backup named:

```
Hackman3D_Orbit_Controller.ino.backup_YYYYMMDD_HHMMSS
```

To restore, rename the backup file back to `Hackman3D_Orbit_Controller.ino` and reflash.

---

## After saving

The `.ino` file is updated but not automatically flashed. Open it in the Arduino IDE and upload to the board as normal to apply the changes.
