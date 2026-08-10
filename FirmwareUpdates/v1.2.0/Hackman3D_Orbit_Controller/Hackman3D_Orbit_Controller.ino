#include "HID.h"
#include <EEPROM.h>
#include <stdlib.h>

#include "orbit_hid_descriptors.h"   // PROGMEM HID descriptor byte arrays
#include "orbit_slicer_hid.h"         // SlicerMouseHID_ class
#include "orbit_logic.h"              // pure math, fixed-point, no Arduino deps
#include "orbit_buttons.h"            // debounce, chord detection, filtering
#include "orbit_serial.h"             // calibration struct + EEPROM persistence

// ============================================================================
// Hackman3D DIY SpaceMouse Firmware
// Firmware for Arduino Pro Micro / ATmega32U4
// Version: 1.2.0
//
// EN: This firmware turns an Arduino Pro Micro into a 6-axis HID SpaceMouse.
// FR: Ce firmware transforme un Arduino Pro Micro en souris 3D HID 6 axes.
//
// Author / Auteur: Hackman3D
//
// Changes in v1.1.0:
//   - Button debounce (10 ms)                        → orbit_buttons.h
//   - EEPROM persistence for speed mode & slicer mode
//   - Non-blocking TX/RX LED feedback
//   - Named constants replacing all magic-number literals
//   - Calibration sanity check with LED error flash
//   - Array sizes use BUTTON_COUNT throughout
//   - Per-slot slicer modifier constants (remapper compatibility)
//   - Chord priority rules documented                → orbit_buttons.h
//   - Pure math extracted to orbit_logic.h (no Arduino deps)
//   - Button/chord code extracted to orbit_buttons.h
//   - HID descriptors extracted to orbit_hid_descriptors.h
//   - SlicerMouseHID_ extracted to orbit_slicer_hid.h
//   - Fixed-point math: applyGain + applyResponseCurve
//     use ×256 integer arithmetic + 64-entry LUT
//     instead of software float on the FPU-less ATmega32U4
//
// Changes in v1.2.0:
//   - Serial calibration protocol (always-on USB CDC, coexists with HID):
//       I  identify            → "ORBIT v1.2.0 CAL:<0|1>"
//       R  raw stream toggle   → "R:<v0>,...,<v7>" absolute ADC at ~50 Hz
//       C  capture center now  → "OK C:<c0>,...,<c7>"
//       D  dump calibration    → "CAL:..." or "CAL:NONE"
//       W  write calibration   → 26 ints: 8 centers, 8 rangeNeg, 8 rangePos,
//                                deadzone, invFlags → saved to EEPROM
//       F  factory reset cal   → "OK"
//   - Per-device calibration stored in EEPROM  → orbit_serial.h
//     (centers, per-channel ranges, deadzone, axis inversions)
//   - Stored centers used at boot when valid (fixes "knob bumped during
//     boot" mis-calibration; startup capture is the fallback)
//   - Per-channel range-normalization gain so weak sensor axes still
//     reach full output
// ============================================================================


// ============================================================================
// SETTINGS / PARAMÈTRES
// ============================================================================

const int DEADZONE_INPUT  = 40;
const int DEADZONE_OUTPUT = 45;
const int CENTER_SAMPLES  = 100;
const int SMOOTH_DIVISOR  = 5;
const int CALIBRATION_MIN = 300;
const int CALIBRATION_MAX = 750;


// ============================================================================
// SENSITIVITY / SENSIBILITÉ
// EN: Float constants kept for documentation; actual math uses the fixed-point
//     equivalents defined in orbit_logic.h (GAIN_TX_FP etc.).
// FR: Constantes flottantes conservées pour la documentation ; le calcul réel
//     utilise les équivalents virgule fixe définis dans orbit_logic.h.
// ============================================================================

const float GAIN_TX = 1.3;
const float GAIN_TY = 1.3;
const float GAIN_TZ = 2.3;
const float GAIN_RX = 1.8;
const float GAIN_RY = 1.8;
const float GAIN_RZ = 2.0;

const float MAX_SPEED_SCALE = 0.70;
const float RESPONSE_CURVE  = 1.6;

const int   SPEED_MODE_COUNT   = 3;
const int   DEFAULT_SPEED_MODE = 1;

// EN: Curve table indexes (orbit_logic.h: 0=1.9 slow, 1=1.6 default, 2=1.3 fast)
// FR: Index de table de courbe (0=1.9 lent, 1=1.6 défaut, 2=1.3 rapide)
const uint8_t SPEED_MODE_CURVE_IDX[SPEED_MODE_COUNT] = { 0, 1, 2 };

const bool         DEBUG_SERIAL              = false;
const unsigned long DEBUG_SERIAL_BAUD        = 115200;
const unsigned long DEBUG_SERIAL_INTERVAL_MS = 100;

const float ROTATION_PRIORITY           = 0.65;
const int   ROTATION_PRIORITY_THRESHOLD = 80;
const int   Z_PUSHPULL_THRESHOLD_MULT   = 2;
const int   Z_ROTATION_THRESHOLD_MULT   = 3;
const int   Z_ROTATION_DIVISOR          = 2;

const bool ENABLE_DOMINANT_AXIS_FILTER = false;


// ============================================================================
// SLICER MOUSE SETTINGS
// ============================================================================

const bool         ENABLE_SLICER_MOUSE_MODE          = true;
const bool         DEFAULT_SLICER_MOUSE_MODE          = false;
const bool         ENABLE_SLICER_KEYBOARD_SHORTCUTS   = true;
const int          SLICER_MOUSE_MOVE_DIVISOR          = 120;
const int          SLICER_MOUSE_WHEEL_THRESHOLD        = 90;
const int          SLICER_MOUSE_WHEEL_FULL_SCALE       = 700;
const int          SLICER_MOUSE_MAX_MOVE               = 12;
const int          SLICER_MOUSE_MAX_WHEEL              = 1;
const unsigned long SLICER_MOUSE_WHEEL_MIN_INTERVAL_MS = 45;
const unsigned long SLICER_MOUSE_WHEEL_MAX_INTERVAL_MS = 125;
const bool         SLICER_MOUSE_AUTO_DRAG              = true;
const uint8_t      SLICER_MOUSE_BUTTON_LEFT            = 0x01;
const uint8_t      SLICER_MOUSE_BUTTON_RIGHT           = 0x02;
const uint8_t      SLICER_MOUSE_BUTTON_MIDDLE          = 0x04;
const uint8_t      SLICER_MOUSE_DRAG_BUTTON            = SLICER_MOUSE_BUTTON_LEFT;

// EN: Per-button shortcut constants — one modifier + key pair per physical
//     button, for both short press and long press. Naming is generic and
//     matches the physical button number (1-3, same numbering shown in the
//     configurator), not any particular slicer command — so remapping
//     tools and users don't need to know what each shortcut is "for".
//     Each of the 12 constants below is independently patchable.
// FR: Constantes de raccourci par bouton — une paire modificateur + touche
//     par bouton physique, pression courte et longue. Le nommage est
//     générique et correspond au numéro du bouton physique (1 à 3, la même
//     numérotation affichée dans le configurateur), pas à une commande
//     logicielle particulière. Les 12 constantes ci-dessous sont modifiables
//     indépendamment.
const uint8_t SLICER_SHORTCUT_MODIFIER_BUTTON1_SHORT = 0x00; // default: Tab
const uint8_t SLICER_SHORTCUT_KEY_BUTTON1_SHORT      = 0x2B; // Tab
const uint8_t SLICER_SHORTCUT_MODIFIER_BUTTON1_LONG  = 0x06; // default: Shift+Alt
const uint8_t SLICER_SHORTCUT_KEY_BUTTON1_LONG       = 0x0A; // G

const uint8_t SLICER_SHORTCUT_MODIFIER_BUTTON2_SHORT = 0x00; // default: N
const uint8_t SLICER_SHORTCUT_KEY_BUTTON2_SHORT      = 0x11; // N
const uint8_t SLICER_SHORTCUT_MODIFIER_BUTTON2_LONG  = 0x00; // default: L
const uint8_t SLICER_SHORTCUT_KEY_BUTTON2_LONG       = 0x0F; // L

const uint8_t SLICER_SHORTCUT_MODIFIER_BUTTON3_SHORT = 0x01; // default: Ctrl+0
const uint8_t SLICER_SHORTCUT_KEY_BUTTON3_SHORT      = 0x27; // 0
const uint8_t SLICER_SHORTCUT_MODIFIER_BUTTON3_LONG  = 0x00; // default: A
const uint8_t SLICER_SHORTCUT_KEY_BUTTON3_LONG       = 0x04; // A

const unsigned long SLICER_BUTTON_LONG_PRESS_MS      = 650;



// ============================================================================
// AXIS INVERSION / INVERSION DES AXES
// ============================================================================

bool invX  = false;
bool invY  = false;
bool invZ  = false;
bool invRX = true;
bool invRY = true;
bool invRZ = true;


// ============================================================================
// PIN CONFIGURATION / CONFIGURATION DES PINS
// ============================================================================

const int pins[8] = { A1, A0, A3, A2, A7, A6, A9, A8 };

const int BUTTON_COUNT                          = 3;
const int buttonPins[BUTTON_COUNT]              = { 2, 3, 7 };
const int MODE_SWITCH_BUTTONS[BUTTON_COUNT]     = { 0, 1, 2 };
const int MODE_SWITCH_BUTTON_COUNT              = 3;
const bool MODE_SWITCH_SUPPRESS_BUTTONS         = true;
const unsigned long MODE_SWITCH_CHORD_WINDOW_MS = 250;
const unsigned long MODE_SWITCH_DEBOUNCE_MS     = 500;

const int SLICER_MODE_BUTTONS[BUTTON_COUNT]     = { 1, 2, 0 };
const int SLICER_MODE_BUTTON_COUNT              = 2;
const unsigned long SLICER_MODE_HOLD_MS         = 250;
const unsigned long SLICER_MODE_DEBOUNCE_MS     = 500;

const int LED_TX_PIN = 30;
const int LED_RX_PIN = 17;

const unsigned long BUTTON_DEBOUNCE_MS  = 10;

const int     EEPROM_MAGIC_ADDR       = 0;
const uint8_t EEPROM_MAGIC_VALUE      = 0xA5;
const int     EEPROM_ADDR_SPEED_MODE  = 1;
const int     EEPROM_ADDR_SLICER_MODE = 2;

// ============================================================================
// SERIAL CALIBRATION PROTOCOL / PROTOCOLE SÉRIE DE CALIBRATION
// ============================================================================

const unsigned long SERIAL_BAUD                = 115200;
const unsigned long RAW_STREAM_INTERVAL_MS     = 20;     // ~50 Hz
const unsigned long RAW_STREAM_TIMEOUT_MS      = 60000;  // auto-off safeguard
const uint8_t       SERIAL_CMD_BUF_SIZE        = 180;
const int           CAL_DEADZONE_MIN           = 5;
const int           CAL_DEADZONE_MAX           = 120;
const int16_t       CAL_NOMINAL_CHANNEL_RANGE  = 512;    // one channel: ±512
const char          FIRMWARE_VERSION_STRING[]  = "ORBIT v1.2.0";


// ============================================================================
// GLOBAL VARIABLES / VARIABLES GLOBALES
// ============================================================================

int  center[8];
bool calibrationFailed = false;

// EN: Runtime calibration state — populated from EEPROM when a valid block
//     exists, otherwise compiled defaults apply.
// FR: État de calibration à l'exécution — chargé depuis l'EEPROM si valide,
//     sinon les valeurs compilées s'appliquent.
OrbitCalibration deviceCal;
bool             deviceCalLoaded      = false;
int              runtimeDeadzoneInput = DEADZONE_INPUT;
int16_t          chanGainFP[8]        = { 256, 256, 256, 256, 256, 256, 256, 256 };

bool          rawStreamEnabled   = false;
unsigned long rawStreamLastAt    = 0;
unsigned long rawStreamStartedAt = 0;
char          serialCmdBuf[SERIAL_CMD_BUF_SIZE];
uint8_t       serialCmdLen       = 0;

int16_t smoothTX = 0, smoothTY = 0, smoothTZ = 0;
int16_t smoothRX = 0, smoothRY = 0, smoothRZ = 0;

int  currentSpeedMode       = DEFAULT_SPEED_MODE;
bool slicerMouseModeEnabled = ENABLE_SLICER_MOUSE_MODE && DEFAULT_SLICER_MOUSE_MODE;

bool modeSwitchComboWasPressed  = false;
bool slicerModeComboWasPressed  = false;
bool slicerModeToggleHandled    = false;

uint8_t      slicerMouseButtonMask                   = 0;
bool         slicerButtonWasPressed[BUTTON_COUNT]    = { false, false, false };
bool         slicerButtonLongHandled[BUTTON_COUNT]   = { false, false, false };
unsigned long slicerButtonPressedAt[BUTTON_COUNT]    = { 0, 0, 0 };
unsigned long slicerModeComboStartedAt  = 0;

unsigned long lastSlicerModeSwitchAt    = 0;
unsigned long lastSlicerMouseWheelAt    = 0;
int8_t        lastSlicerMouseWheelDirection = 0;
unsigned long lastModeSwitchAt          = 0;

uint32_t      debouncedButtonMask  = 0;
uint32_t      lastRawButtonMask    = 0;
unsigned long lastButtonChangeAt   = 0;

int          ledBlinkRemaining = 0;
bool         ledBlinkState     = false;
unsigned long ledBlinkLastAt   = 0;
const unsigned long LED_BLINK_ON_MS  = 80;
const unsigned long LED_BLINK_OFF_MS = 120;

unsigned long rxLedHoldUntil = 0;
const unsigned long RX_LED_HOLD_MS = 500;

// EN: SlicerMouseHID_ instance — constructed via orbit_slicer_hid.h.
// FR: Instance SlicerMouseHID_ — construite via orbit_slicer_hid.h.
SlicerMouseHID_ SlicerMouseHID(ENABLE_SLICER_MOUSE_MODE,
                                ENABLE_SLICER_KEYBOARD_SHORTCUTS);


// ============================================================================
// LED HELPERS
// ============================================================================

void ledStartBlink(int count) {
  ledBlinkRemaining = count;
  ledBlinkState     = true;
  ledBlinkLastAt    = millis();
  digitalWrite(LED_TX_PIN, LOW);
}

void ledUpdateBlink() {
  if (ledBlinkRemaining <= 0) return;
  unsigned long now = millis();
  if (ledBlinkState) {
    if (now - ledBlinkLastAt >= LED_BLINK_ON_MS) {
      ledBlinkState = false; ledBlinkLastAt = now;
      digitalWrite(LED_TX_PIN, HIGH);
    }
  } else {
    if (now - ledBlinkLastAt >= LED_BLINK_OFF_MS) {
      if (--ledBlinkRemaining > 0) {
        ledBlinkState = true; ledBlinkLastAt = now;
        digitalWrite(LED_TX_PIN, LOW);
      }
    }
  }
}

void ledSignalSpeedMode(int mode)      { ledStartBlink(mode + 1); }

void ledSignalSlicerMode(bool enabled) {
  if (enabled) { rxLedHoldUntil = millis() + RX_LED_HOLD_MS; digitalWrite(LED_RX_PIN, LOW); }
  else         { rxLedHoldUntil = 0; digitalWrite(LED_RX_PIN, HIGH); }
}

void ledUpdateRx() {
  if (rxLedHoldUntil > 0 && millis() >= rxLedHoldUntil) {
    rxLedHoldUntil = 0; digitalWrite(LED_RX_PIN, HIGH);
  }
}

void ledFlashCalibrationError() {
  for (int i = 0; i < 6; i++) {
    digitalWrite(LED_TX_PIN, LOW);  delay(60);
    digitalWrite(LED_TX_PIN, HIGH); delay(60);
  }
}


// ============================================================================
// EEPROM PERSISTENCE
// ============================================================================

void eepromLoad() {
  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VALUE) return;
  currentSpeedMode = constrain((int)EEPROM.read(EEPROM_ADDR_SPEED_MODE),
                               0, SPEED_MODE_COUNT - 1);
  slicerMouseModeEnabled = ENABLE_SLICER_MOUSE_MODE &&
                           (EEPROM.read(EEPROM_ADDR_SLICER_MODE) != 0);
}

void eepromSave() {
  EEPROM.update(EEPROM_ADDR_SPEED_MODE,  (uint8_t)currentSpeedMode);
  EEPROM.update(EEPROM_ADDR_SLICER_MODE, slicerMouseModeEnabled ? 1 : 0);
  EEPROM.update(EEPROM_MAGIC_ADDR,       EEPROM_MAGIC_VALUE);
}


// ============================================================================
// readAxes() / calibrateCenter()
// ============================================================================

void readAxes(int* values) {
  for (int i = 0; i < 8; i++) values[i] = analogRead(pins[i]);
}

void calibrateCenter() {
  long sum[8] = {0};
  for (int n = 0; n < CENTER_SAMPLES; n++) {
    int temp[8]; readAxes(temp);
    for (int i = 0; i < 8; i++) sum[i] += temp[i];
    delay(5);
  }
  calibrationFailed = false;
  for (int i = 0; i < 8; i++) {
    center[i] = sum[i] / CENTER_SAMPLES;
    if (center[i] < CALIBRATION_MIN || center[i] > CALIBRATION_MAX)
      calibrationFailed = true;
  }
  if (calibrationFailed) ledFlashCalibrationError();
}


// ============================================================================
// applyDeviceCalibration()
// EN: Applies a loaded OrbitCalibration to the runtime state: stored centers,
//     noise-derived input deadzone, axis inversions, and per-channel
//     range-normalization gains (measured range → nominal ±512 full scale).
// FR: Applique une calibration chargée : centres stockés, zone morte dérivée
//     du bruit, inversions d'axes et gains de normalisation par canal.
// ============================================================================

void applyDeviceCalibration() {
  for (int i = 0; i < 8; i++) {
    center[i]     = deviceCal.center[i];
    chanGainFP[i] = orbitCalGainFP(CAL_NOMINAL_CHANNEL_RANGE,
                                   orbitCalChannelSpan(deviceCal, i));
  }

  runtimeDeadzoneInput = constrain((int)deviceCal.deadzoneInput,
                                   CAL_DEADZONE_MIN, CAL_DEADZONE_MAX);

  invX  = (deviceCal.invFlags & 0x01) != 0;
  invY  = (deviceCal.invFlags & 0x02) != 0;
  invZ  = (deviceCal.invFlags & 0x04) != 0;
  invRX = (deviceCal.invFlags & 0x08) != 0;
  invRY = (deviceCal.invFlags & 0x10) != 0;
  invRZ = (deviceCal.invFlags & 0x20) != 0;

  calibrationFailed = false;
}


// ============================================================================
// resetDeviceCalibration()
// EN: Restores compiled defaults after a factory reset (F command).
// FR: Restaure les valeurs compilées après réinitialisation usine.
// ============================================================================

void resetDeviceCalibration() {
  deviceCalLoaded      = false;
  runtimeDeadzoneInput = DEADZONE_INPUT;
  for (int i = 0; i < 8; i++) chanGainFP[i] = 256;
  invX  = false; invY  = false; invZ  = false;
  invRX = true;  invRY = true;  invRZ = true;
  calibrateCenter();
}


// ============================================================================
// resetSmoothing() / sendCommand()
// ============================================================================

void resetSmoothing() {
  smoothTX=0; smoothTY=0; smoothTZ=0;
  smoothRX=0; smoothRY=0; smoothRZ=0;
}

void sendCommand(int16_t rx, int16_t ry, int16_t rz,
                 int16_t x,  int16_t y,  int16_t z) {
  uint8_t trans[6] = {
    (uint8_t)(x&0xFF),(uint8_t)(x>>8),
    (uint8_t)(y&0xFF),(uint8_t)(y>>8),
    (uint8_t)(z&0xFF),(uint8_t)(z>>8)
  };
  uint8_t rot[6] = {
    (uint8_t)(rx&0xFF),(uint8_t)(rx>>8),
    (uint8_t)(ry&0xFF),(uint8_t)(ry>>8),
    (uint8_t)(rz&0xFF),(uint8_t)(rz>>8)
  };
  HID().SendReport(1, trans, 6);
  HID().SendReport(2, rot,   6);
}


// ============================================================================
// updateSpeedMode() / updateSlicerMouseMode()
// ============================================================================

void updateSpeedMode(bool comboPressed) {
  unsigned long now = millis();
  if (comboPressed && !modeSwitchComboWasPressed &&
      now - lastModeSwitchAt >= MODE_SWITCH_DEBOUNCE_MS) {
    if (++currentSpeedMode >= SPEED_MODE_COUNT) currentSpeedMode = 0;
    lastModeSwitchAt = now;
    resetSmoothing();
    eepromSave();
    ledSignalSpeedMode(currentSpeedMode);
  }
  modeSwitchComboWasPressed = comboPressed;
}

void updateSlicerMouseMode(bool comboPressed) {
  unsigned long now = millis();
  if (comboPressed && !slicerModeComboWasPressed) {
    slicerModeComboStartedAt = now; slicerModeToggleHandled = false;
  }
  if (comboPressed && !slicerModeToggleHandled &&
      now - slicerModeComboStartedAt >= SLICER_MODE_HOLD_MS &&
      now - lastSlicerModeSwitchAt   >= SLICER_MODE_DEBOUNCE_MS) {
    slicerMouseModeEnabled = !slicerMouseModeEnabled;
    lastSlicerModeSwitchAt = now; slicerModeToggleHandled = true;
    releaseSlicerMouseButtons();
    resetSmoothing(); eepromSave();
    ledSignalSlicerMode(slicerMouseModeEnabled);
  }
  if (!comboPressed) slicerModeToggleHandled = false;
  slicerModeComboWasPressed = comboPressed;
}


// ============================================================================
// Slicer mouse helpers
// ============================================================================

void sendSlicerMouseReport(int8_t x, int8_t y, int8_t wheel) {
  SlicerMouseHID.sendReport(slicerMouseButtonMask, x, y, wheel);
}

void setSlicerMouseButton(uint8_t button, bool pressed) {
  uint8_t prev = slicerMouseButtonMask;
  if (pressed) slicerMouseButtonMask |=  button;
  else         slicerMouseButtonMask &= ~button;
  if (slicerMouseButtonMask != prev) sendSlicerMouseReport(0, 0, 0);
}

void releaseSlicerMouseButtons() {
  if (slicerMouseButtonMask == 0) return;
  slicerMouseButtonMask = 0;
  sendSlicerMouseReport(0, 0, 0);
}

void sendSlicerKeyboardShortcut(uint8_t modifiers, uint8_t key) {
  if (!ENABLE_SLICER_KEYBOARD_SHORTCUTS) return;
  SlicerMouseHID.sendKeyboardReport(modifiers, key);
  delay(20);
  SlicerMouseHID.sendKeyboardReport(0, 0);
}

void resetSlicerButtonActions() {
  for (int i = 0; i < BUTTON_COUNT; i++) {
    slicerButtonWasPressed[i] = false;
    slicerButtonLongHandled[i] = false;
    slicerButtonPressedAt[i]   = 0;
  }
  if (ENABLE_SLICER_KEYBOARD_SHORTCUTS) SlicerMouseHID.sendKeyboardReport(0, 0);
}

// EN: Per-button shortcut lookup tables — index 0..BUTTON_COUNT-1 maps
//     directly to physical button 1..3. Replaces the old action-indirection
//     (SLICER_BUTTON_ACTION_* / SLICER_BUTTON_ACTIONS[]) with a direct,
//     generic button->shortcut mapping.
// FR: Tables de raccourcis par bouton — l'index 0..BUTTON_COUNT-1 correspond
//     directement au bouton physique 1 à 3. Remplace l'ancienne indirection
//     par action par une correspondance directe et générique bouton->raccourci.
const uint8_t SLICER_SHORTCUT_MODIFIER_SHORT[BUTTON_COUNT] = {
  SLICER_SHORTCUT_MODIFIER_BUTTON1_SHORT,
  SLICER_SHORTCUT_MODIFIER_BUTTON2_SHORT,
  SLICER_SHORTCUT_MODIFIER_BUTTON3_SHORT,
};
const uint8_t SLICER_SHORTCUT_KEY_SHORT[BUTTON_COUNT] = {
  SLICER_SHORTCUT_KEY_BUTTON1_SHORT,
  SLICER_SHORTCUT_KEY_BUTTON2_SHORT,
  SLICER_SHORTCUT_KEY_BUTTON3_SHORT,
};
const uint8_t SLICER_SHORTCUT_MODIFIER_LONG[BUTTON_COUNT] = {
  SLICER_SHORTCUT_MODIFIER_BUTTON1_LONG,
  SLICER_SHORTCUT_MODIFIER_BUTTON2_LONG,
  SLICER_SHORTCUT_MODIFIER_BUTTON3_LONG,
};
const uint8_t SLICER_SHORTCUT_KEY_LONG[BUTTON_COUNT] = {
  SLICER_SHORTCUT_KEY_BUTTON1_LONG,
  SLICER_SHORTCUT_KEY_BUTTON2_LONG,
  SLICER_SHORTCUT_KEY_BUTTON3_LONG,
};

void runSlicerButtonAction(int buttonIndex, bool longPress) {
  releaseSlicerMouseButtons();
  sendSlicerKeyboardShortcut(
    longPress ? SLICER_SHORTCUT_MODIFIER_LONG[buttonIndex]  : SLICER_SHORTCUT_MODIFIER_SHORT[buttonIndex],
    longPress ? SLICER_SHORTCUT_KEY_LONG[buttonIndex]       : SLICER_SHORTCUT_KEY_SHORT[buttonIndex]);
}

void updateSlicerMouseButtons(uint32_t buttonMask, bool suppressButtons) {
  if (suppressButtons) { releaseSlicerMouseButtons(); resetSlicerButtonActions(); return; }
  if (!ENABLE_SLICER_KEYBOARD_SHORTCUTS) return;
  unsigned long now = millis();
  for (int i = 0; i < BUTTON_COUNT; i++) {
    bool pressed = (buttonMask & (1UL << i)) != 0;
    if (pressed && !slicerButtonWasPressed[i]) {
      slicerButtonWasPressed[i] = true; slicerButtonLongHandled[i] = false;
      slicerButtonPressedAt[i]  = now;
    }
    if (pressed && !slicerButtonLongHandled[i] &&
        now - slicerButtonPressedAt[i] >= SLICER_BUTTON_LONG_PRESS_MS) {
      runSlicerButtonAction(i, true); slicerButtonLongHandled[i] = true;
    }
    if (!pressed && slicerButtonWasPressed[i]) {
      if (!slicerButtonLongHandled[i]) runSlicerButtonAction(i, false);
      slicerButtonWasPressed[i] = false; slicerButtonLongHandled[i] = false;
      slicerButtonPressedAt[i]  = 0;
    }
  }
}


int8_t scaleMouseAxis(int16_t value, int divisor, int maxVal) {
  if (abs(value) < DEADZONE_OUTPUT) return 0;
  int s = value / divisor;
  if (s == 0) s = (value > 0) ? 1 : -1;
  if (s >  maxVal) s =  maxVal;
  if (s < -maxVal) s = -maxVal;
  return (int8_t)s;
}

int8_t scaleMouseWheel(int16_t value) {
  int mag = abs(value);
  if (mag < SLICER_MOUSE_WHEEL_THRESHOLD) { lastSlicerMouseWheelDirection = 0; return 0; }
  unsigned long now = millis();
  int capped = (mag > SLICER_MOUSE_WHEEL_FULL_SCALE) ? SLICER_MOUSE_WHEEL_FULL_SCALE : mag;
  int range  = SLICER_MOUSE_WHEEL_FULL_SCALE - SLICER_MOUSE_WHEEL_THRESHOLD;
  if (range < 1) range = 1;
  unsigned long iRange = SLICER_MOUSE_WHEEL_MAX_INTERVAL_MS - SLICER_MOUSE_WHEEL_MIN_INTERVAL_MS;
  unsigned long interval = SLICER_MOUSE_WHEEL_MAX_INTERVAL_MS -
                           ((unsigned long)(capped - SLICER_MOUSE_WHEEL_THRESHOLD) * iRange / range);
  int8_t dir = (value > 0) ? -SLICER_MOUSE_MAX_WHEEL : SLICER_MOUSE_MAX_WHEEL;
  if (dir == lastSlicerMouseWheelDirection && now - lastSlicerMouseWheelAt < interval) return 0;
  lastSlicerMouseWheelAt = now; lastSlicerMouseWheelDirection = dir;
  return dir;
}

void sendSlicerMouse(int16_t tx, int16_t ty, int16_t tz,
                     int16_t rx, int16_t ry, int16_t rz) {
  int   tPow = abs(tx)+abs(ty), rPow = abs(rx)+abs(ry)+abs(rz);
  bool  zoom = abs(tz) >= SLICER_MOUSE_WHEEL_THRESHOLD;
  int8_t wheel = scaleMouseWheel(tz), mouseX = 0, mouseY = 0;
  if (zoom) { releaseSlicerMouseButtons(); if (wheel) sendSlicerMouseReport(0,0,wheel); return; }
  if (!SLICER_MOUSE_AUTO_DRAG) {
    if (rPow > tPow && rPow > DEADZONE_OUTPUT) {
      mouseX = scaleMouseAxis(ry+rz, SLICER_MOUSE_MOVE_DIVISOR, SLICER_MOUSE_MAX_MOVE);
      mouseY = scaleMouseAxis(rx,    SLICER_MOUSE_MOVE_DIVISOR, SLICER_MOUSE_MAX_MOVE);
    } else if (tPow > DEADZONE_OUTPUT) {
      mouseX = scaleMouseAxis(tx, SLICER_MOUSE_MOVE_DIVISOR, SLICER_MOUSE_MAX_MOVE);
      mouseY = scaleMouseAxis(ty, SLICER_MOUSE_MOVE_DIVISOR, SLICER_MOUSE_MAX_MOVE);
    }
    if (mouseX||mouseY||wheel) sendSlicerMouseReport(mouseX, mouseY, wheel);
    return;
  }
  if (rPow > tPow && rPow > DEADZONE_OUTPUT) {
    setSlicerMouseButton(SLICER_MOUSE_DRAG_BUTTON, true);
    mouseX = scaleMouseAxis(ry+rz, SLICER_MOUSE_MOVE_DIVISOR, SLICER_MOUSE_MAX_MOVE);
    mouseY = scaleMouseAxis(rx,    SLICER_MOUSE_MOVE_DIVISOR, SLICER_MOUSE_MAX_MOVE);
  } else if (tPow > DEADZONE_OUTPUT) {
    setSlicerMouseButton(SLICER_MOUSE_DRAG_BUTTON, true);
    mouseX = scaleMouseAxis(tx, SLICER_MOUSE_MOVE_DIVISOR, SLICER_MOUSE_MAX_MOVE);
    mouseY = scaleMouseAxis(ty, SLICER_MOUSE_MOVE_DIVISOR, SLICER_MOUSE_MAX_MOVE);
  } else { releaseSlicerMouseButtons(); }
  if (mouseX||mouseY||wheel) sendSlicerMouseReport(mouseX, mouseY, wheel);
}


// ============================================================================
// sendButtons()
// ============================================================================

void sendButtons(uint32_t buttonMask) {
  uint8_t buttons[4] = {0,0,0,0};
  for (int i = 0; i < BUTTON_COUNT; i++)
    if ((buttonMask & (1UL<<i)) != 0) buttons[i/8] |= (1<<(i%8));
  HID().SendReport(3, buttons, 4);
}


// ============================================================================
// debugPrintState()
// ============================================================================

void debugPrintState(const int* v,
                     int16_t tx, int16_t ty, int16_t tz,
                     int16_t rx, int16_t ry, int16_t rz,
                     uint32_t buttonMask) {
  if (!DEBUG_SERIAL) return;
  static unsigned long lastAt = 0;
  unsigned long now = millis();
  if (now - lastAt < DEBUG_SERIAL_INTERVAL_MS) return;
  lastAt = now;
  Serial.print("mode:");     Serial.print(currentSpeedMode);
  Serial.print(" slicer:");  Serial.print(slicerMouseModeEnabled ? 1 : 0);
  Serial.print(" calFail:"); Serial.print(calibrationFailed ? 1 : 0);
  Serial.print(" buttons:"); Serial.print(buttonMask);
  for (int i=0;i<8;i++){Serial.print(" v");Serial.print(i);Serial.print(":");Serial.print(v[i]);}
  Serial.print(" tx:"); Serial.print(tx);
  Serial.print(" ty:"); Serial.print(ty);
  Serial.print(" tz:"); Serial.print(tz);
  Serial.print(" rx:"); Serial.print(rx);
  Serial.print(" ry:"); Serial.print(ry);
  Serial.print(" rz:"); Serial.println(rz);
}


// ============================================================================
// SERIAL CALIBRATION PROTOCOL HANDLER
// EN: Line-based single-letter commands over USB CDC. Any received command
//     stops the raw stream so a stuck stream can never interfere.
// FR: Commandes d'une lettre terminées par saut de ligne via USB CDC.
// ============================================================================

void serialPrintCsv8(const int16_t* vals) {
  for (int i = 0; i < 8; i++) {
    Serial.print(vals[i]);
    if (i < 7) Serial.print(',');
  }
}

void serialCmdIdentify() {
  Serial.print(FIRMWARE_VERSION_STRING);
  Serial.print(F(" CAL:"));
  Serial.println(deviceCalLoaded ? 1 : 0);
}

void serialCmdCaptureCenter() {
  calibrateCenter();
  Serial.print(F("OK C:"));
  for (int i = 0; i < 8; i++) {
    Serial.print(center[i]);
    if (i < 7) Serial.print(',');
  }
  Serial.println();
}

void serialCmdDump() {
  if (!deviceCalLoaded) { Serial.println(F("CAL:NONE")); return; }
  Serial.print(F("CAL:"));
  serialPrintCsv8(deviceCal.center);   Serial.print(';');
  serialPrintCsv8(deviceCal.rangeNeg); Serial.print(';');
  serialPrintCsv8(deviceCal.rangePos); Serial.print(';');
  Serial.print(deviceCal.deadzoneInput); Serial.print(';');
  Serial.println(deviceCal.invFlags);
}

// EN: W <c0..c7> <n0..n7> <p0..p7> <deadzone> <invFlags>  (26 integers)
// FR: W <c0..c7> <n0..n7> <p0..p7> <zone morte> <invFlags> (26 entiers)
void serialCmdWrite(char* args) {
  long vals[26];
  char* p = args;
  for (int i = 0; i < 26; i++) {
    char* end;
    vals[i] = strtol(p, &end, 10);
    if (end == p) { Serial.println(F("ERR PARSE")); return; }
    p = end;
  }

  OrbitCalibration cal;
  for (int i = 0; i < 8; i++) {
    long c = vals[i], n = vals[8 + i], pos = vals[16 + i];
    if (c < 0 || c > 1023 || n > 0 || n < -1023 || pos < 0 || pos > 1023) {
      Serial.println(F("ERR RANGE")); return;
    }
    cal.center[i]   = (int16_t)c;
    cal.rangeNeg[i] = (int16_t)n;
    cal.rangePos[i] = (int16_t)pos;
  }
  long dz = vals[24], inv = vals[25];
  if (dz < CAL_DEADZONE_MIN || dz > CAL_DEADZONE_MAX || inv < 0 || inv > 63) {
    Serial.println(F("ERR RANGE")); return;
  }
  cal.deadzoneInput = (uint8_t)dz;
  cal.invFlags      = (uint8_t)inv;

  orbitCalSave(cal);
  deviceCal       = cal;
  deviceCalLoaded = true;
  applyDeviceCalibration();
  resetSmoothing();
  Serial.println(F("OK"));
}

void handleSerialCommand(char* line) {
  // EN: Any command halts the raw stream except R itself (which toggles it).
  // FR: Toute commande arrête le flux brut sauf R (qui le bascule).
  char cmd = line[0];
  if (cmd != 'R' && cmd != 'r') rawStreamEnabled = false;

  switch (cmd) {
    case 'I': case 'i':
      serialCmdIdentify();
      break;
    case 'R': case 'r':
      rawStreamEnabled = !rawStreamEnabled;
      if (rawStreamEnabled) rawStreamStartedAt = millis();
      Serial.println(rawStreamEnabled ? F("R:ON") : F("R:OFF"));
      break;
    case 'C': case 'c':
      serialCmdCaptureCenter();
      break;
    case 'D': case 'd':
      serialCmdDump();
      break;
    case 'W': case 'w':
      serialCmdWrite(line + 1);
      break;
    case 'F': case 'f':
      orbitCalErase();
      resetDeviceCalibration();
      Serial.println(F("OK"));
      break;
    default:
      Serial.println(F("ERR CMD"));
      break;
  }
}

void processSerial() {
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (serialCmdLen > 0) {
        serialCmdBuf[serialCmdLen] = '\0';
        handleSerialCommand(serialCmdBuf);
        serialCmdLen = 0;
      }
    } else if (serialCmdLen < SERIAL_CMD_BUF_SIZE - 1) {
      serialCmdBuf[serialCmdLen++] = ch;
    } else {
      serialCmdLen = 0;  // overflow — discard line
    }
  }
}

void updateRawStream() {
  if (!rawStreamEnabled) return;
  unsigned long now = millis();
  if (now - rawStreamStartedAt >= RAW_STREAM_TIMEOUT_MS) {
    rawStreamEnabled = false;
    Serial.println(F("R:OFF"));
    return;
  }
  if (now - rawStreamLastAt < RAW_STREAM_INTERVAL_MS) return;
  rawStreamLastAt = now;

  int raw[8];
  readAxes(raw);
  Serial.print(F("R:"));
  for (int i = 0; i < 8; i++) {
    Serial.print(raw[i]);
    if (i < 7) Serial.print(',');
  }
  Serial.println();
}


// ============================================================================
// setup()
// ============================================================================

void setup() {
  static HIDSubDescriptor node(hidReportDescriptor, sizeof(hidReportDescriptor));
  HID().AppendDescriptor(&node);

  // EN: Serial is always started — required by the calibration protocol.
  //     USB CDC coexists with the HID interfaces and costs nothing when idle.
  // FR: Le port série est toujours démarré — requis par le protocole de
  //     calibration. Le CDC USB coexiste avec le HID.
  Serial.begin(SERIAL_BAUD);

  for (int i = 0; i < BUTTON_COUNT; i++) pinMode(buttonPins[i], INPUT_PULLUP);

  pinMode(LED_TX_PIN, OUTPUT); digitalWrite(LED_TX_PIN, HIGH);
  pinMode(LED_RX_PIN, OUTPUT); digitalWrite(LED_RX_PIN, HIGH);

  eepromLoad();
  delay(800);

  // EN: Prefer stored calibration (centers, deadzone, inversions, gains) —
  //     it fixes the "knob bumped during boot" problem. Fall back to the
  //     classic startup center capture when no valid block exists.
  // FR: Priorité à la calibration stockée ; repli sur la capture de centre
  //     au démarrage si aucun bloc valide n'existe.
  deviceCalLoaded = orbitCalLoad(deviceCal);
  if (deviceCalLoaded) {
    applyDeviceCalibration();
  } else {
    calibrateCenter();
  }

  ledSignalSpeedMode(currentSpeedMode);
}


// ============================================================================
// loop()
// ============================================================================

void loop() {
  // --- Serial calibration protocol ------------------------------------------
  processSerial();
  updateRawStream();

  // --- Buttons (orbit_buttons.h) -------------------------------------------
  uint32_t buttonMask = readDebouncedButtons(
    buttonPins, BUTTON_COUNT, BUTTON_DEBOUNCE_MS,
    debouncedButtonMask, lastRawButtonMask, lastButtonChangeAt);

  bool modeSwitchComboPressed = isModeSwitchComboPressed(
    buttonMask, MODE_SWITCH_BUTTONS, MODE_SWITCH_BUTTON_COUNT, BUTTON_COUNT);

  bool slicerModeComboPressed = isSlicerModeComboPressed(
    buttonMask, SLICER_MODE_BUTTONS, SLICER_MODE_BUTTON_COUNT, BUTTON_COUNT,
    ENABLE_SLICER_MOUSE_MODE);

  bool modeSwitchComboAccepted = false;
  uint32_t hidButtonMask = filterModeSwitchButtons(
    buttonMask, modeSwitchComboPressed, MODE_SWITCH_SUPPRESS_BUTTONS,
    MODE_SWITCH_BUTTONS, MODE_SWITCH_BUTTON_COUNT, BUTTON_COUNT,
    MODE_SWITCH_CHORD_WINDOW_MS, modeSwitchComboAccepted);

  hidButtonMask = filterSlicerModeButtons(
    hidButtonMask, slicerModeComboPressed, ENABLE_SLICER_MOUSE_MODE,
    SLICER_MODE_BUTTONS, SLICER_MODE_BUTTON_COUNT, BUTTON_COUNT);

  updateSpeedMode(modeSwitchComboAccepted);
  updateSlicerMouseMode(slicerModeComboPressed);

  // --- Joystick reading -----------------------------------------------------
  int raw[8], v[8];
  readAxes(raw);
  for (int i = 0; i < 8; i++) {
    v[i] = raw[i] - center[i];
    // EN: Per-channel range normalization from stored calibration (256 = 1.0×).
    // FR: Normalisation de plage par canal issue de la calibration (256 = 1,0×).
    if (chanGainFP[i] != 256)
      v[i] = (int)(((int32_t)v[i] * chanGainFP[i]) >> 8);
  }
  applyInputDeadzone(v, 8, runtimeDeadzoneInput);   // orbit_logic.h

  // --- Axis calculation -----------------------------------------------------
  int16_t transX = v[5] - v[1];
  int16_t transY = v[7] - v[3];
  int16_t transZ = 0;
  int16_t rotX   = v[4] - v[0];
  int16_t rotY   = v[2] - v[6];
  int16_t rotZ   = 0;

  // Z push/pull
  int zPushPull = v[0]+v[2]+v[4]+v[6];
  if (((countPositive4(v[0],v[2],v[4],v[6],runtimeDeadzoneInput)>=3) ||
       (countNegative4(v[0],v[2],v[4],v[6],runtimeDeadzoneInput)>=3)) &&
      abs(zPushPull) > runtimeDeadzoneInput * Z_PUSHPULL_THRESHOLD_MULT) {
    transZ = -zPushPull; transX = 0; transY = 0;
  }

  // Z rotation
  int zTwist = v[1]+v[3]+v[5]+v[7];
  if (((countPositive4(v[1],v[3],v[5],v[7],runtimeDeadzoneInput)>=3) ||
       (countNegative4(v[1],v[3],v[5],v[7],runtimeDeadzoneInput)>=3)) &&
      abs(zTwist) > runtimeDeadzoneInput * Z_ROTATION_THRESHOLD_MULT) {
    rotZ = zTwist / Z_ROTATION_DIVISOR; rotX = 0; rotY = 0;
  }

  // Rotation priority
  int rPow = abs(rotX)+abs(rotY)+abs(rotZ);
  int tPow = abs(transX)+abs(transY)+abs(transZ);
  if (rPow > ROTATION_PRIORITY_THRESHOLD &&
      rPow > (int32_t)(tPow * ROTATION_PRIORITY * 256) >> 8) {
    transX=0; transY=0; transZ=0;
    smoothTX=0; smoothTY=0; smoothTZ=0;
  }

  // Gains — fixed-point ×256 (orbit_logic.h)
  transX = applyGain(transX, GAIN_TX_FP);
  transY = applyGain(transY, GAIN_TY_FP);
  transZ = applyGain(transZ, GAIN_TZ_FP);
  rotX   = applyGain(rotX,   GAIN_RX_FP);
  rotY   = applyGain(rotY,   GAIN_RY_FP);
  rotZ   = applyGain(rotZ,   GAIN_RZ_FP);

  // Dominant axis filter (orbit_logic.h)
  if (ENABLE_DOMINANT_AXIS_FILTER)
    keepOnlyDominantAxis(transX, transY, transZ, rotX, rotY, rotZ);

  // Response curve — fixed-point LUT (orbit_logic.h)
  int16_t  sFP  = SPEED_SCALE_FP[currentSpeedMode];
  uint8_t  cIdx = SPEED_MODE_CURVE_IDX[currentSpeedMode];
  transX = applyResponseCurve(transX, INPUT_MAX_TX_FP256, sFP, cIdx, DEADZONE_OUTPUT);
  transY = applyResponseCurve(transY, INPUT_MAX_TY_FP256, sFP, cIdx, DEADZONE_OUTPUT);
  transZ = applyResponseCurve(transZ, INPUT_MAX_TZ_FP256, sFP, cIdx, DEADZONE_OUTPUT);
  rotX   = applyResponseCurve(rotX,   INPUT_MAX_RX_FP256, sFP, cIdx, DEADZONE_OUTPUT);
  rotY   = applyResponseCurve(rotY,   INPUT_MAX_RY_FP256, sFP, cIdx, DEADZONE_OUTPUT);
  rotZ   = applyResponseCurve(rotZ,   INPUT_MAX_RZ_FP256, sFP, cIdx, DEADZONE_OUTPUT);

  // Output deadzone (orbit_logic.h)
  applyOutputDeadzone(transX, transY, transZ, rotX, rotY, rotZ, DEADZONE_OUTPUT);

  // Axis inversion
  if (invX)  transX = -transX;
  if (invY)  transY = -transY;
  if (invZ)  transZ = -transZ;
  if (invRX) rotX   = -rotX;
  if (invRY) rotY   = -rotY;
  if (invRZ) rotZ   = -rotZ;

  // Smoothing (orbit_logic.h)
  smoothTX = smoothValue(smoothTX, transX, SMOOTH_DIVISOR);
  smoothTY = smoothValue(smoothTY, transY, SMOOTH_DIVISOR);
  smoothTZ = smoothValue(smoothTZ, transZ, SMOOTH_DIVISOR);
  smoothRX = smoothValue(smoothRX, rotX,   SMOOTH_DIVISOR);
  smoothRY = smoothValue(smoothRY, rotY,   SMOOTH_DIVISOR);
  smoothRZ = smoothValue(smoothRZ, rotZ,   SMOOTH_DIVISOR);

  // Final output deadzone (orbit_logic.h)
  int16_t oTX=smoothTX, oTY=smoothTY, oTZ=smoothTZ;
  int16_t oRX=smoothRX, oRY=smoothRY, oRZ=smoothRZ;
  applyOutputDeadzone(oTX, oTY, oTZ, oRX, oRY, oRZ, DEADZONE_OUTPUT);

  // --- HID output -----------------------------------------------------------
  if (slicerMouseModeEnabled) {
    sendCommand(0,0,0,0,0,0);
    sendButtons(0);
    updateSlicerMouseButtons(buttonMask, modeSwitchComboPressed || slicerModeComboPressed);
    sendSlicerMouse(oTX, oTY, oTZ, oRX, oRY, oRZ);
  } else {
    releaseSlicerMouseButtons();
    sendCommand(oRX, oRZ, oRY, oTX, oTZ, oTY);
    sendButtons(hidButtonMask);
  }

  debugPrintState(v, oTX, oTY, oTZ, oRX, oRY, oRZ, buttonMask);

  // --- LED update -----------------------------------------------------------
  ledUpdateBlink();
  ledUpdateRx();
}
