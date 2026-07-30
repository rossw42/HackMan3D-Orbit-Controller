#pragma once

// ============================================================================
// orbit_slicer_hid.h
// EN: SlicerMouseHID_ — a separate USB HID interface providing a relative
//     mouse (Report ID 1) and an optional keyboard (Report ID 2) for slicer
//     mouse emulation mode.  Descriptors are pulled from orbit_hid_descriptors.h.
// FR: SlicerMouseHID_ — interface USB HID séparée fournissant une souris
//     relative (ID 1) et un clavier optionnel (ID 2) pour le mode slicer.
//     Les descripteurs viennent de orbit_hid_descriptors.h.
// ============================================================================

#include <Arduino.h>
#include "HID.h"
#include "orbit_hid_descriptors.h"


class SlicerMouseHID_ : public PluggableUSBModule {
public:
  SlicerMouseHID_(bool enableSlicer, bool enableKeyboard);

  // EN: Send a relative mouse report (buttons, X, Y, wheel).
  // FR: Envoie un rapport souris relatif (boutons, X, Y, molette).
  int sendReport(uint8_t buttons, int8_t x, int8_t y, int8_t wheel);

  // EN: Send a keyboard report (modifier byte + one keycode).
  // FR: Envoie un rapport clavier (modificateur + un code touche).
  int sendKeyboardReport(uint8_t modifiers, uint8_t key);

protected:
  int     getInterface(uint8_t* interfaceCount);
  int     getDescriptor(USBSetup& setup);
  bool    setup(USBSetup& setup);
  uint8_t getShortName(char* name);

private:
  bool    _enableKeyboard;
  uint8_t epType[1];
  uint8_t protocol;
  uint8_t idle;
};


// ─── Implementation ─────────────────────────────────────────────────────────

inline SlicerMouseHID_::SlicerMouseHID_(bool enableSlicer, bool enableKeyboard)
  : PluggableUSBModule(1, 1, epType),
    _enableKeyboard(enableKeyboard),
    protocol(HID_REPORT_PROTOCOL),
    idle(1)
{
  epType[0] = EP_TYPE_INTERRUPT_IN;
  HID();
  if (enableSlicer) PluggableUSB().plug(this);
}

inline int SlicerMouseHID_::getInterface(uint8_t* interfaceCount) {
  *interfaceCount += 1;
  uint16_t descriptorSize = sizeof(mouseReportDescriptor);
  if (_enableKeyboard) descriptorSize += sizeof(keyboardReportDescriptor);

  HIDDescriptor hidInterface = {
    D_INTERFACE(pluggedInterface, 1, USB_DEVICE_CLASS_HUMAN_INTERFACE,
                HID_SUBCLASS_BOOT_INTERFACE, HID_PROTOCOL_MOUSE),
    D_HIDREPORT(descriptorSize),
    D_ENDPOINT(USB_ENDPOINT_IN(pluggedEndpoint), USB_ENDPOINT_TYPE_INTERRUPT,
               USB_EP_SIZE, 0x01)
  };
  return USB_SendControl(0, &hidInterface, sizeof(hidInterface));
}

inline int SlicerMouseHID_::getDescriptor(USBSetup& setup) {
  if (setup.bmRequestType != REQUEST_DEVICETOHOST_STANDARD_INTERFACE) return 0;
  if (setup.wValueH       != HID_REPORT_DESCRIPTOR_TYPE)              return 0;
  if (setup.wIndex        != pluggedInterface)                         return 0;

  protocol = HID_REPORT_PROTOCOL;

  int r = USB_SendControl(TRANSFER_PGM, mouseReportDescriptor,
                          sizeof(mouseReportDescriptor));
  if (r < 0 || !_enableKeyboard) return r;

  int r2 = USB_SendControl(TRANSFER_PGM, keyboardReportDescriptor,
                           sizeof(keyboardReportDescriptor));
  return (r2 < 0) ? r2 : r + r2;
}

inline bool SlicerMouseHID_::setup(USBSetup& setup) {
  if (setup.wIndex != pluggedInterface) return false;

  uint8_t req     = setup.bRequest;
  uint8_t reqType = setup.bmRequestType;

  if (reqType == REQUEST_DEVICETOHOST_CLASS_INTERFACE) {
    if (req == HID_GET_REPORT || req == HID_GET_PROTOCOL || req == HID_GET_IDLE)
      return true;
  }
  if (reqType == REQUEST_HOSTTODEVICE_CLASS_INTERFACE) {
    if (req == HID_SET_PROTOCOL) { protocol = setup.wValueL; return true; }
    if (req == HID_SET_IDLE)     { idle     = setup.wValueL; return true; }
    if (req == HID_SET_REPORT)   return true;
  }
  return false;
}

inline uint8_t SlicerMouseHID_::getShortName(char* name) {
  name[0] = 'M'; name[1] = 'O'; name[2] = 'U'; return 3;
}

inline int SlicerMouseHID_::sendReport(uint8_t buttons,
                                       int8_t x, int8_t y, int8_t wheel) {
  uint8_t reportId  = 1;
  uint8_t report[4] = { buttons, (uint8_t)x, (uint8_t)y, (uint8_t)wheel };
  int r = USB_Send(pluggedEndpoint, &reportId, 1);
  if (r < 0) return r;
  int r2 = USB_Send(pluggedEndpoint | TRANSFER_RELEASE, report, 4);
  return (r2 < 0) ? r2 : r + r2;
}

inline int SlicerMouseHID_::sendKeyboardReport(uint8_t modifiers, uint8_t key) {
  if (!_enableKeyboard) return 0;
  uint8_t reportId  = 2;
  uint8_t report[8] = { modifiers, 0, key, 0, 0, 0, 0, 0 };
  int r = USB_Send(pluggedEndpoint, &reportId, 1);
  if (r < 0) return r;
  int r2 = USB_Send(pluggedEndpoint | TRANSFER_RELEASE, report, 8);
  return (r2 < 0) ? r2 : r + r2;
}
