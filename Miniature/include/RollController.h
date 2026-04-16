#pragma once

// ============================================================
//  RollController.h  –  PID roulis  (IMU roll → foils arrière)
//  RC Hydrofoil – Teensy 4.1
//
//  Actif uniquement en état CONTROLE.
//
//  Setpoint: ROLL_SETPOINT_DEG = 0° (ailes à plat)
//
//  Sortie: correction différentielle sur foils arrière
//    foil_rear_L += +output   (aile gauche monte)
//    foil_rear_R += -output   (aile droite descend)
// ============================================================

#include <Arduino.h>
#include <Config.h>

class RollController {
public:
    RollController() = default;

    void begin();
    void reset();   // remet intégrale à zéro — appeler à l'entrée en CONTROLE

    /**
     * @brief  Calcule la correction PID roulis.
     * @param  rollDeg  Angle de roulis IMU [deg]  (+= aile droite basse)
     * @param  dt       Pas de temps [s]
     * @return Correction différentielle [deg] à appliquer aux foils arrière
     */
    float update(float rollDeg, float dt);

    float getSetpoint()  const { return ROLL_SETPOINT_DEG; }
    float getError()     const { return _error;            }
    float getOutput()    const { return _output;           }

    void printDebug()    const;

private:
    float _error     = 0.0f;
    float _integral  = 0.0f;
    float _prevError = 0.0f;
    float _output    = 0.0f;
};