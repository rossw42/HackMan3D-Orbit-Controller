#pragma once

// ============================================================================
// orbit_hid_descriptors.h
// EN: HID report descriptor byte arrays stored in PROGMEM.
//     Separated so they can be reviewed or replaced independently of the
//     SlicerMouseHID_ class and the main sketch.
// FR: Tableaux d'octets du descripteur HID stockés en PROGMEM.
//     Séparés pour pouvoir être révisés ou remplacés indépendamment.
// ============================================================================

#include <Arduino.h>
#include <avr/pgmspace.h>


// ============================================================================
// hidReportDescriptor
// EN: 3Dconnexion-compatible multi-axis controller.
//     Report 1 = Translation X/Y/Z (int16, little-endian)
//     Report 2 = Rotation RX/RY/RZ (int16, little-endian)
//     Report 3 = 32 buttons (1 bit each)
// FR: Contrôleur multi-axes compatible 3Dconnexion.
// ============================================================================

static const uint8_t hidReportDescriptor[] PROGMEM = {
  0x05, 0x01, 0x09, 0x08, 0xA1, 0x01,

  // Translation report / Rapport de translation
  0xA1, 0x00,
  0x85, 0x01,
  0x16, 0x00, 0x80,
  0x26, 0xFF, 0x7F,
  0x36, 0x00, 0x80,
  0x46, 0xFF, 0x7F,
  0x09, 0x30, 0x09, 0x31, 0x09, 0x32,
  0x75, 0x10,
  0x95, 0x03,
  0x81, 0x02,
  0xC0,

  // Rotation report / Rapport de rotation
  0xA1, 0x00,
  0x85, 0x02,
  0x16, 0x00, 0x80,
  0x26, 0xFF, 0x7F,
  0x36, 0x00, 0x80,
  0x46, 0xFF, 0x7F,
  0x09, 0x33, 0x09, 0x34, 0x09, 0x35,
  0x75, 0x10,
  0x95, 0x03,
  0x81, 0x02,
  0xC0,

  // Button report / Rapport des boutons
  0xA1, 0x00,
  0x85, 0x03,
  0x15, 0x00,
  0x25, 0x01,
  0x75, 0x01,
  0x95, 32,
  0x05, 0x09,
  0x19, 1,
  0x29, 32,
  0x81, 0x02,
  0xC0,

  0xC0
};


// ============================================================================
// mouseReportDescriptor
// EN: Standard relative USB mouse — buttons (3), X, Y, Wheel.
//     Used by the slicer mouse emulation interface (Report ID 1).
// FR: Souris USB relative standard — boutons (3), X, Y, molette.
// ============================================================================

static const uint8_t mouseReportDescriptor[] PROGMEM = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x02,        // Usage (Mouse)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  0x09, 0x01,        //   Usage (Pointer)
  0xA1, 0x00,        //   Collection (Physical)
  0x05, 0x09,        //     Usage Page (Button)
  0x19, 0x01,        //     Usage Minimum (Button 1)
  0x29, 0x03,        //     Usage Maximum (Button 3)
  0x15, 0x00,        //     Logical Minimum (0)
  0x25, 0x01,        //     Logical Maximum (1)
  0x95, 0x03,        //     Report Count (3)
  0x75, 0x01,        //     Report Size (1)
  0x81, 0x02,        //     Input (Data, Variable, Absolute)
  0x95, 0x01,        //     Report Count (1)
  0x75, 0x05,        //     Report Size (5)
  0x81, 0x03,        //     Input (Constant, Variable, Absolute)
  0x05, 0x01,        //     Usage Page (Generic Desktop)
  0x09, 0x30,        //     Usage (X)
  0x09, 0x31,        //     Usage (Y)
  0x09, 0x38,        //     Usage (Wheel)
  0x15, 0x81,        //     Logical Minimum (-127)
  0x25, 0x7F,        //     Logical Maximum (127)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x03,        //     Report Count (3)
  0x81, 0x06,        //     Input (Data, Variable, Relative)
  0xC0,              //   End Collection
  0xC0               // End Collection
};


// ============================================================================
// keyboardReportDescriptor
// EN: Minimal boot-compatible keyboard — modifier byte + 1 keycode.
//     Used by the slicer keyboard shortcut interface (Report ID 2).
// FR: Clavier minimal compatible boot — octet modificateur + 1 code touche.
// ============================================================================

static const uint8_t keyboardReportDescriptor[] PROGMEM = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x06,        // Usage (Keyboard)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x02,        //   Report ID (2)
  0x05, 0x07,        //   Usage Page (Keyboard)
  0x19, 0xE0,        //   Usage Minimum (Keyboard LeftControl)
  0x29, 0xE7,        //   Usage Maximum (Keyboard Right GUI)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x08,        //   Report Count (8)
  0x81, 0x02,        //   Input (Data, Variable, Absolute)
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x08,        //   Report Size (8)
  0x81, 0x03,        //   Input (Constant, Variable, Absolute)
  0x95, 0x06,        //   Report Count (6)
  0x75, 0x08,        //   Report Size (8)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x73,        //   Logical Maximum (115)
  0x05, 0x07,        //   Usage Page (Keyboard)
  0x19, 0x00,        //   Usage Minimum (Reserved)
  0x29, 0x73,        //   Usage Maximum (Keyboard Application)
  0x81, 0x00,        //   Input (Data, Array, Absolute)
  0xC0               // End Collection
};
