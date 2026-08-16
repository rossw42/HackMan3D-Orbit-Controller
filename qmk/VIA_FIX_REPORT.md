# VIA Custom Menus - Fix Report

**Date:** August 15, 2026  
**Issue:** VIA not loading custom menus/parameter controls  
**Status:** ✅ FIXED

---

## Problem

The calibrate.html was working and receiving HID signals, but VIA wasn't displaying the custom tuning menus or key remapping controls.

---

## Root Cause: `hackman3d_orbit_controller_phase8.json` Was a Stub

The file `qmk/via/hackman3d_orbit_controller_phase8.json` was **missing the entire `menus` section** — it was saved as an incomplete stub with only the device identity and layout:

```json
{
  "name": "HackMan3D Orbit Controller",
  "vendorId": "0x256F",
  "productId": "0xC631",
  "matrix": { "rows": 1, "cols": 3 },
  "layouts": {
    "keymap": [["0,0"], ["0,1"], ["0,2"]]
  }
  // ❌ No "menus" section — all 50 custom parameters were missing!
}
```

## VIA Schema

This VIA installation uses **V2 definitions**, which requires:
- `"vendorId"` as a hex string (e.g. `"0x256F"`)
- `"productId"` as a hex string (e.g. `"0xC631"`)
- No extra properties (no `vendorProductId` — that's a V3 field)
- The `"menus"` array to expose custom parameter tabs

---

## Changes Made

### 1. `qmk/via/hackman3d_orbit_controller_phase8.json`
- ✅ Restored complete `"menus"` section — all 50 live-editable parameters across 5 tabs
- ✅ Format kept as VIA V2 (`vendorId` / `productId` strings)

### 2. `qmk/via/hackman3d_orbit_controller.json`
- ✅ Already had complete menus — no content changes needed
- ✅ Format confirmed correct VIA V2 format

---

## VIA Menu Structure (both files)

50 live-editable parameters across 5 tabs:

| Tab | Parameters | Function |
|---|---|---|
| **Axes** | 15 | Deadzones, gains (×256), axis inversion |
| **Speed** | 7 | Speed modes, acceleration curves |
| **Filters** | 6 | Rotation priority, Z-axis detection |
| **Slicer** | 11 | Mouse mode tuning, wheel settings |
| **System** | 11 | Button timings, calibration, recalibrate action |

---

## How to Use VIA

1. **Open VIA** — https://usevia.app/ (Chrome/Edge only)
2. **Enable Design tab:** VIA Settings → Show Design tab ✓
3. **Load the definition:**
   - Go to Design tab
   - Click "Load Draft Definition"
   - Select `qmk/via/hackman3d_orbit_controller.json`
4. **Connect controller** → device appears automatically
5. **Use custom menus** to tune parameters live — changes go to RAM immediately, click Save to write EEPROM

> **Note:** VIA V2 format uses `vendorId` / `productId` as hex strings. Do **not** add `vendorProductId` — that field is V3 only and will cause a schema validation error.

---

## Verification

Both JSON files now contain:
- ✅ `"vendorId": "0x256F"` and `"productId": "0xC631"` (V2 format)
- ✅ Complete `"menus"` array with 5 tabs
- ✅ Valid JSON (confirmed with python json.tool)
- ✅ No extra properties that would fail V2 schema validation
