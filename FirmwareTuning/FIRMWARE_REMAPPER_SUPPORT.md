# Firmware Changes to Fully Support the Remapper

Currently the remapper can only edit shortcuts that have a named constant in the `.ino`. Several slots have their modifier hardcoded as `0` directly in `runSlicerButtonAction()`, which means the remapper has nothing to patch for those slots.

This document describes the minimal changes needed to make every slot fully configurable.

---

## The problem

`runSlicerButtonAction()` currently looks like this for the PAINT action:

```cpp
if (action == SLICER_BUTTON_ACTION_PAINT) {
    if (longPress) {
        sendSlicerKeyboardShortcut(0, SLICER_SHORTCUT_KEY_L);  // modifier hardcoded
    } else {
        sendSlicerKeyboardShortcut(0, SLICER_SHORTCUT_KEY_N);  // modifier hardcoded
    }
    return;
}
```

The `0` is a bare literal — there is no constant to patch. The same applies to HOME long press and TAB short press.

The one shared constant `SLICER_SHORTCUT_MODIFIER_PRIMARY` is used by both HOME short press and TAB long press, so changing it affects both slots simultaneously.

---

## Fix: add one modifier constant per slot

### 1. Add six new constants in the settings section

Replace the existing shared constant:

```cpp
// EN: Modifier key used for primary shortcuts.
// FR: Touche modificatrice utilisée pour les raccourcis principaux.
const uint8_t SLICER_SHORTCUT_MODIFIER_PRIMARY = 0x08; // macOS Command
```

With one constant per action per press type:

```cpp
// EN: Modifier keys for each slicer button action (short and long press).
// FR: Touches modificatrices pour chaque action de bouton slicer.

const uint8_t SLICER_SHORTCUT_MODIFIER_HOME_SHORT  = 0x08; // Cmd+0
const uint8_t SLICER_SHORTCUT_MODIFIER_HOME_LONG   = 0x00; // A
const uint8_t SLICER_SHORTCUT_MODIFIER_PAINT_SHORT = 0x00; // N
const uint8_t SLICER_SHORTCUT_MODIFIER_PAINT_LONG  = 0x00; // L
const uint8_t SLICER_SHORTCUT_MODIFIER_TAB_SHORT   = 0x00; // Tab
const uint8_t SLICER_SHORTCUT_MODIFIER_TAB_LONG    = 0x08; // Cmd+Shift+G
```

> The values above preserve the existing default behaviour exactly.

---

### 2. Update `runSlicerButtonAction()` to use the new constants

```cpp
void runSlicerButtonAction(int action, bool longPress) {
  releaseSlicerMouseButtons();

  if (action == SLICER_BUTTON_ACTION_HOME) {
    if (longPress) {
      sendSlicerKeyboardShortcut(SLICER_SHORTCUT_MODIFIER_HOME_LONG,  SLICER_SHORTCUT_KEY_A);
    } else {
      sendSlicerKeyboardShortcut(SLICER_SHORTCUT_MODIFIER_HOME_SHORT, SLICER_SHORTCUT_KEY_0);
    }
    return;
  }

  if (action == SLICER_BUTTON_ACTION_PAINT) {
    if (longPress) {
      sendSlicerKeyboardShortcut(SLICER_SHORTCUT_MODIFIER_PAINT_LONG,  SLICER_SHORTCUT_KEY_L);
    } else {
      sendSlicerKeyboardShortcut(SLICER_SHORTCUT_MODIFIER_PAINT_SHORT, SLICER_SHORTCUT_KEY_N);
    }
    return;
  }

  if (action == SLICER_BUTTON_ACTION_TAB_SEND) {
    if (longPress) {
      sendSlicerKeyboardShortcut(SLICER_SHORTCUT_MODIFIER_TAB_LONG,
                                 SLICER_SHORTCUT_KEY_G);
    } else {
      sendSlicerKeyboardShortcut(SLICER_SHORTCUT_MODIFIER_TAB_SHORT, SLICER_SHORTCUT_KEY_TAB);
    }
  }
}
```

---

### 3. Update the remapper UI

Once the firmware has the six individual constants, update `ACTION_SHORTCUTS` in `remap.html` to reference them:

```js
const ACTION_SHORTCUTS = {
  HOME: {
    short: { modConst: 'SLICER_SHORTCUT_MODIFIER_HOME_SHORT',  keyConst: 'SLICER_SHORTCUT_KEY_0'   },
    long:  { modConst: 'SLICER_SHORTCUT_MODIFIER_HOME_LONG',   keyConst: 'SLICER_SHORTCUT_KEY_A'   },
  },
  PAINT: {
    short: { modConst: 'SLICER_SHORTCUT_MODIFIER_PAINT_SHORT', keyConst: 'SLICER_SHORTCUT_KEY_N'   },
    long:  { modConst: 'SLICER_SHORTCUT_MODIFIER_PAINT_LONG',  keyConst: 'SLICER_SHORTCUT_KEY_L'   },
  },
  TAB: {
    short: { modConst: 'SLICER_SHORTCUT_MODIFIER_TAB_SHORT',   keyConst: 'SLICER_SHORTCUT_KEY_TAB' },
    long:  { modConst: 'SLICER_SHORTCUT_MODIFIER_TAB_LONG',    keyConst: 'SLICER_SHORTCUT_KEY_G'   },
  },
};
```

Also add the six new constant names to `SHORTCUT_CONSTANTS` and remove `SLICER_SHORTCUT_MODIFIER_PRIMARY`:

```js
const SHORTCUT_CONSTANTS = [
  'SLICER_SHORTCUT_MODIFIER_HOME_SHORT',
  'SLICER_SHORTCUT_MODIFIER_HOME_LONG',
  'SLICER_SHORTCUT_MODIFIER_PAINT_SHORT',
  'SLICER_SHORTCUT_MODIFIER_PAINT_LONG',
  'SLICER_SHORTCUT_MODIFIER_TAB_SHORT',
  'SLICER_SHORTCUT_MODIFIER_TAB_LONG',
  'SLICER_SHORTCUT_KEY_0', 'SLICER_SHORTCUT_KEY_A',
  'SLICER_SHORTCUT_KEY_G', 'SLICER_SHORTCUT_KEY_L',
  'SLICER_SHORTCUT_KEY_N', 'SLICER_SHORTCUT_KEY_TAB',
];
```

After this change every slot will show a modifier dropdown in the remapper, and all six modifier + key combinations are independently configurable without touching the firmware again.

---

## Summary of files changed

| File | Change |
|------|--------|
| `Hackman3D_Orbit_Controller.ino` | Replace `SLICER_SHORTCUT_MODIFIER_PRIMARY` with 6 per-slot constants; update `runSlicerButtonAction()` |
| `remap.html` | Update `ACTION_SHORTCUTS` and `SHORTCUT_CONSTANTS` to reference the new constant names |
