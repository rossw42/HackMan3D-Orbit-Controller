#pragma once

// ============================================================================
// orbit_serial.h
// EN: Calibration data structure, EEPROM persistence helpers and fixed-point
//     calibration-gain math for the serial calibration protocol (v1.2.0).
//     The serial command handler itself lives in the .ino (it needs access to
//     most firmware globals); this header holds the reusable pieces.
//
//     EEPROM layout:
//       addr 0..2   : v1.1.0 settings (magic, speed mode, slicer mode)
//       addr 16..69 : OrbitCalibration struct (54 bytes, versioned+checksummed)
//
// FR: Structure des données de calibration, persistance EEPROM et calculs de
//     gain de calibration en virgule fixe pour le protocole série (v1.2.0).
//     Le gestionnaire de commandes série est dans le .ino ; cet en-tête
//     contient les éléments réutilisables.
// ============================================================================

#include <Arduino.h>
#include <EEPROM.h>

// EN: Magic / version / address for the calibration block. Bump the version
//     whenever the struct layout changes — old blocks are then ignored.
// FR: Magic / version / adresse du bloc de calibration. Incrémenter la version
//     à chaque changement de structure — les anciens blocs sont ignorés.
const uint16_t ORBIT_CAL_MAGIC       = 0xC1A2;
const uint8_t  ORBIT_CAL_VERSION     = 1;
const int      ORBIT_CAL_EEPROM_ADDR = 16;

// ============================================================================
// OrbitCalibration
// EN: 54 bytes on AVR (no struct padding on 8-bit targets).
//     center[]   : absolute ADC rest position per channel (0..1023)
//     rangeNeg[] : most negative deflection seen, relative to center (<= 0)
//     rangePos[] : most positive deflection seen, relative to center (>= 0)
//     deadzoneInput : measured-noise-derived input deadzone
//     invFlags   : bit0..bit5 = invX, invY, invZ, invRX, invRY, invRZ
// FR: 54 octets sur AVR. Centres ADC absolus, déflexions min/max relatives au
//     centre, zone morte dérivée du bruit mesuré, drapeaux d'inversion.
// ============================================================================

struct OrbitCalibration {
  uint16_t magic;
  uint8_t  version;
  int16_t  center[8];
  int16_t  rangeNeg[8];
  int16_t  rangePos[8];
  uint8_t  deadzoneInput;
  uint8_t  invFlags;
  uint8_t  checksum;   // XOR of every preceding byte
};

// EN: XOR checksum over the whole struct except the trailing checksum byte.
// FR: Somme de contrôle XOR sur toute la structure sauf l'octet final.
inline uint8_t orbitCalComputeChecksum(const OrbitCalibration& cal) {
  const uint8_t* p = (const uint8_t*)&cal;
  uint8_t sum = 0;
  for (size_t i = 0; i < sizeof(OrbitCalibration) - 1; i++) sum ^= p[i];
  return sum;
}

// EN: Loads the calibration block. Returns true only when magic, version and
//     checksum all validate — otherwise the caller keeps compiled defaults.
// FR: Charge le bloc de calibration. Retourne true seulement si magic, version
//     et somme de contrôle sont valides.
inline bool orbitCalLoad(OrbitCalibration& cal) {
  EEPROM.get(ORBIT_CAL_EEPROM_ADDR, cal);
  if (cal.magic   != ORBIT_CAL_MAGIC)            return false;
  if (cal.version != ORBIT_CAL_VERSION)          return false;
  if (orbitCalComputeChecksum(cal) != cal.checksum) return false;
  return true;
}

// EN: Stamps magic/version/checksum and writes the block (EEPROM.put uses
//     update semantics — unchanged bytes are not rewritten).
// FR: Écrit magic/version/checksum puis le bloc (octets inchangés non réécrits).
inline void orbitCalSave(OrbitCalibration& cal) {
  cal.magic    = ORBIT_CAL_MAGIC;
  cal.version  = ORBIT_CAL_VERSION;
  cal.checksum = orbitCalComputeChecksum(cal);
  EEPROM.put(ORBIT_CAL_EEPROM_ADDR, cal);
}

// EN: Invalidates the stored calibration (factory reset). Only the magic is
//     destroyed; the next load fails and defaults apply.
// FR: Invalide la calibration stockée (réinitialisation usine).
inline void orbitCalErase() {
  EEPROM.update(ORBIT_CAL_EEPROM_ADDR,     0xFF);
  EEPROM.update(ORBIT_CAL_EEPROM_ADDR + 1, 0xFF);
}

// ============================================================================
// orbitCalChannelSpan()
// EN: Average usable deflection magnitude of one channel:
//     (rangePos - rangeNeg) / 2, floored at 1 to avoid divide-by-zero.
// FR: Amplitude moyenne utilisable d'un canal, plancher à 1.
// ============================================================================

inline int16_t orbitCalChannelSpan(const OrbitCalibration& cal, int i) {
  int32_t span = ((int32_t)cal.rangePos[i] - (int32_t)cal.rangeNeg[i]) / 2;
  if (span < 1) span = 1;
  return (int16_t)span;
}

// ============================================================================
// orbitCalGainFP()
// EN: Fixed-point (×256) correction gain that rescales a measured axis range
//     back to its nominal full-scale, so a weak sensor axis still reaches full
//     output. Clamped to 0.25×–4.0× so a bad calibration can never produce a
//     wild gain.
// FR: Gain de correction virgule fixe (×256) ramenant la plage mesurée à la
//     pleine échelle nominale. Borné entre 0,25× et 4,0×.
// ============================================================================

inline int16_t orbitCalGainFP(int32_t nominalRange, int32_t measuredRange) {
  if (measuredRange < 1) measuredRange = 1;
  int32_t g = (nominalRange * 256L) / measuredRange;
  if (g < 64)   g = 64;    // 0.25×
  if (g > 1024) g = 1024;  // 4.0×
  return (int16_t)g;
}