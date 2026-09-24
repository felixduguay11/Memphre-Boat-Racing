#pragma once

// ============================================================
//  HeightController.h  –  PID hauteur  (Sonar → foils)
//  RC Hydrofoil – Teensy 4.1
//
//  Actif uniquement en état CONTROLE.
//
//  Setpoint:
//    - Fixe : HEIGHT_SETPOINT_DEFAULT_CM (Config.h)
//    - Ajustable : knob CH6 mappe [−1,+1] →
//                  [HEIGHT_SETPOINT_MIN_CM, HEIGHT_SETPOINT_MAX_CM]
//
//  Sortie: correction en degrés appliquée aux 3 foils
//    foil_front    = FOIL_ANGLE_NEUTRAL + output
//    foil_rear_L   = FOIL_ANGLE_NEUTRAL + output
//    foil_rear_R   = FOIL_ANGLE_NEUTRAL + output  (+ roll correction)
// ============================================================

#include <Arduino.h>
#include <Config.h>

class HeightController {
public:
    HeightController() = default;

    void begin();
    void reset();   // remet intégrale à zéro — appeler à l'entrée en CONTROLE

    /**
     * @brief  Calcule la correction PID hauteur.
     * @param  measuredCm   Distance sonar [cm]
     * @param  knob         Valeur knob CH6 [-1, +1]  (ignoré si FIXED_SETPOINT)
     * @param  dt           Pas de temps [s]
     * @return Correction en degrés à ajouter aux foils  [deg]
     */
    float update(float measuredCm, float knob, float dt);

    float getSetpoint()    const { return _setpoint;    }
    float getError()       const { return _error;       }
    float getOutput()      const { return _output;      }

    void printDebug()      const;

private:
    float _setpoint   = HEIGHT_SETPOINT_DEFAULT_CM;
    float _error      = 0.0f;
    float _integral   = 0.0f;
    float _prevError  = 0.0f;
    float _output     = 0.0f;

    void updateSetpoint(float knob);
};