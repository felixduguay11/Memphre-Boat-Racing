#pragma once

// ============================================================
//  ESC.h  –  Pilote double ESC (PWM)
//  Memphre Boat Racing – Teensy 4.1
//
//  Porté depuis Miniature/include/ESC.h.
//  Modif vs original : include "config.h" (minuscule, guillemets).
//
//  Câblage (depuis config.h) :
//    PIN_ESC_LEFT  → ESC gauche  (pin 2)
//    PIN_ESC_RIGHT → ESC droit   (pin 3)
//
//  PWM : 50 Hz, 1000–2000 µs
//    1000 µs = pleine marche arrière
//    1500 µs = stop
//    2000 µs = pleine marche avant
//
//  Armement : envoyer ESC_PULSE_MIN_US pendant ESC_ARM_DELAY_MS.
//  Le sens des moteurs est géré par le câblage des ESC.
//
//  ⚠ toDeg() map [-1,+1] → 0..180° et PWMServo convertit ensuite
//    le degré en µs (≈544..2400 µs). Voir note dans mon message si
//    ton ESC exige exactement 1000/1500/2000 µs.
// ============================================================

#include <Arduino.h>
#include <PWMServo.h>
#include "config.h"

class ESC {
public:
    ESC() = default;

    // Attache les pins PWM et arme les deux ESC.
    // Bloque (via vTaskDelay) pendant ESC_ARM_DELAY_MS.
    void begin();

    // Envoie la même commande aux deux moteurs.
    //   throttle [-1.0,+1.0] : +1 = pleine avant, -1 = pleine arrière, 0 = stop
    void set(float throttle);

    // Stop immédiat — pulse neutre aux deux ESC.
    void stop();

    // Désarme — position sécuritaire.
    void disarm();

    float getThrottle() const { return _throttle; }
    bool  isArmed()     const { return _armed;    }

    void printDebug() const;

private:
    PWMServo _escLeft;
    PWMServo _escRight;

    float _throttle = 0.0f;
    bool  _armed    = false;

    uint16_t toPulse(float throttle) const;
};
