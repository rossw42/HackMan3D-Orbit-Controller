#pragma once

// ============================================================================
// orbit_buttons.h
// EN: Button reading, debounce, chord detection, and mode filtering.
//     Depends on Arduino (digitalRead, millis) — not PC-testable as-is,
//     but all chord logic is separated from HID/analog hardware.
// FR: Lecture des boutons, anti-rebond, détection des accords, filtrage.
//     Dépend d'Arduino (digitalRead, millis) — pas testable sur PC tel quel,
//     mais la logique des accords est séparée du matériel HID/analogique.
//
// CHORD PRIORITY RULES (must not be broken by future edits):
//   - Slicer toggle fires ONLY when exactly the slicer buttons are held with
//     no other buttons pressed (buttonMask == comboMask).
//   - Speed mode fires ONLY when ALL mode-switch buttons are held.
//   - Two of the three buttons are shared between both chords.
//     The exact-match requirement for the slicer combo ensures the two chords
//     are mutually exclusive: pressing all 3 buttons cannot trigger the slicer
//     toggle because the full mask never equals the 2-button slicer mask.
//
// FR: RÈGLES DE PRIORITÉ DES ACCORDS :
//   - Le basculement slicer se déclenche UNIQUEMENT quand exactement les
//     boutons slicer sont enfoncés sans autre bouton (buttonMask == comboMask).
//   - Le changement de mode vitesse se déclenche UNIQUEMENT quand tous les
//     boutons mode-switch sont enfoncés simultanément.
//   - La correspondance exacte pour le slicer garantit l'exclusion mutuelle.
// ============================================================================

#include <Arduino.h>
#include <stdint.h>


// ============================================================================
// Chord state — defined here so the .ino can declare them as externs or
// include this header (the inline/static pattern keeps them in one TU).
// ============================================================================

static bool     _modeSwitchChordActive          = false;
static bool     _modeSwitchChordComboTriggered   = false;
static bool     _modeSwitchButtonsForwarded      = false;
static uint32_t _modeSwitchPendingButtons        = 0;
static unsigned long _modeSwitchChordStartedAt   = 0;


// ============================================================================
// resetModeSwitchChord()
// EN: Clears temporary state used while detecting a speed-mode button chord.
// FR: Réinitialise l'état temporaire de détection de la combinaison de boutons.
// ============================================================================

inline void resetModeSwitchChord() {
  _modeSwitchChordActive         = false;
  _modeSwitchChordComboTriggered = false;
  _modeSwitchButtonsForwarded    = false;
  _modeSwitchPendingButtons      = 0;
}


// ============================================================================
// readDebouncedButtons()
// EN: Reads physical buttons and applies a "stable for debounceMs" filter.
//     Returns the last confirmed stable mask, preventing switch bounce from
//     triggering spurious mode changes or long-press events.
// FR: Lit les boutons physiques et applique un filtre de stabilité.
//     Retourne le dernier masque stable confirmé.
// ============================================================================

inline uint32_t readDebouncedButtons(const int* buttonPins, int buttonCount,
                                     unsigned long debounceMs,
                                     uint32_t &debouncedMask,
                                     uint32_t &lastRawMask,
                                     unsigned long &lastChangeAt) {
  uint32_t raw = 0;

  for (int i = 0; i < buttonCount; i++) {
    if (digitalRead(buttonPins[i]) == LOW) {
      raw |= (1UL << i);
    }
  }

  if (raw != lastRawMask) {
    lastRawMask  = raw;
    lastChangeAt = millis();
  } else if (millis() - lastChangeAt >= debounceMs) {
    debouncedMask = raw;
  }

  return debouncedMask;
}


// ============================================================================
// getButtonComboMask()
// EN: Builds a bit mask from an array of button indexes.
// FR: Crée un masque à partir d'un tableau d'index de boutons.
// ============================================================================

inline uint32_t getButtonComboMask(const int* buttonIndexes, int comboCount,
                                   int buttonCount) {
  if (comboCount <= 0 || comboCount > buttonCount) {
    return 0;
  }

  uint32_t mask = 0;

  for (int i = 0; i < comboCount; i++) {
    int idx = buttonIndexes[i];
    if (idx < 0 || idx >= buttonCount) {
      return 0;
    }
    mask |= (1UL << idx);
  }

  return mask;
}


// ============================================================================
// isModeSwitchComboPressed()
// EN: Checks if all configured mode-switch buttons are pressed (subset match).
// FR: Vérifie si tous les boutons mode-switch sont appuyés (correspondance sous-ensemble).
// ============================================================================

inline bool isModeSwitchComboPressed(uint32_t buttonMask,
                                     const int* modeSwitchButtons,
                                     int modeSwitchCount,
                                     int buttonCount) {
  uint32_t comboMask = getButtonComboMask(modeSwitchButtons, modeSwitchCount,
                                          buttonCount);
  if (comboMask == 0) return false;
  return (buttonMask & comboMask) == comboMask;
}


// ============================================================================
// isSlicerModeComboPressed()
// EN: Checks if ONLY the slicer-mode buttons are pressed — exact match.
//     See chord priority rules at the top of this file.
// FR: Vérifie si UNIQUEMENT les boutons slicer sont appuyés — correspondance exacte.
// ============================================================================

inline bool isSlicerModeComboPressed(uint32_t buttonMask,
                                     const int* slicerButtons,
                                     int slicerCount,
                                     int buttonCount,
                                     bool slicerEnabled) {
  if (!slicerEnabled) return false;
  uint32_t comboMask = getButtonComboMask(slicerButtons, slicerCount,
                                          buttonCount);
  if (comboMask == 0) return false;
  return buttonMask == comboMask;
}


// ============================================================================
// filterModeSwitchButtons()
// EN: Suppresses mode-switch buttons while a chord is being detected.
//     Sets comboAccepted = true when the full chord fires.
// FR: Bloque les boutons mode-switch pendant la détection de la combinaison.
// ============================================================================

inline uint32_t filterModeSwitchButtons(uint32_t buttonMask,
                                        bool comboPressed,
                                        bool suppressButtons,
                                        const int* modeSwitchButtons,
                                        int modeSwitchCount,
                                        int buttonCount,
                                        unsigned long chordWindowMs,
                                        bool &comboAccepted) {
  comboAccepted = false;

  if (!suppressButtons) {
    comboAccepted = comboPressed;
    return buttonMask;
  }

  uint32_t switchMask = getButtonComboMask(modeSwitchButtons, modeSwitchCount,
                                           buttonCount);
  if (switchMask == 0) return buttonMask;

  unsigned long now = millis();
  uint32_t nonSwitchButtons    = buttonMask & ~switchMask;
  uint32_t activeSwitchButtons = buttonMask &  switchMask;

  if (activeSwitchButtons == 0) {
    _modeSwitchChordActive = false;

    if (!_modeSwitchChordComboTriggered &&
        !_modeSwitchButtonsForwarded &&
        _modeSwitchPendingButtons != 0) {
      uint32_t released = _modeSwitchPendingButtons;
      resetModeSwitchChord();
      return nonSwitchButtons | released;
    }

    resetModeSwitchChord();
    return nonSwitchButtons;
  }

  if (!_modeSwitchChordActive) {
    _modeSwitchChordActive         = true;
    _modeSwitchChordComboTriggered = false;
    _modeSwitchButtonsForwarded    = false;
    _modeSwitchPendingButtons      = activeSwitchButtons;
    _modeSwitchChordStartedAt      = now;
  } else {
    _modeSwitchPendingButtons |= activeSwitchButtons;
  }

  if (comboPressed) {
    _modeSwitchChordComboTriggered = true;
    comboAccepted = true;
    return nonSwitchButtons;
  }

  if (_modeSwitchChordComboTriggered) {
    comboAccepted = true;
    return nonSwitchButtons;
  }

  if (now - _modeSwitchChordStartedAt < chordWindowMs) {
    return nonSwitchButtons;
  }

  _modeSwitchButtonsForwarded = true;
  return buttonMask;
}


// ============================================================================
// filterSlicerModeButtons()
// EN: Suppresses slicer-mode combo buttons while toggling mode.
// FR: Bloque les boutons slicer pendant le basculement de mode.
// ============================================================================

inline uint32_t filterSlicerModeButtons(uint32_t buttonMask,
                                        bool comboPressed,
                                        bool slicerEnabled,
                                        const int* slicerButtons,
                                        int slicerCount,
                                        int buttonCount) {
  if (!slicerEnabled || !comboPressed) return buttonMask;

  uint32_t comboMask = getButtonComboMask(slicerButtons, slicerCount,
                                          buttonCount);
  if (comboMask == 0) return buttonMask;

  return buttonMask & ~comboMask;
}
