#pragma once

// ============================================================
//  ESC.h  –  Dual ESC PWM driver
//  RC Hydrofoil – Teensy 4.1
//
//  Wiring (from Config.h):
//    PIN_ESC_LEFT  → ESC gauche  (pin 2)
//    PIN_ESC_RIGHT → ESC droit   (pin 3)
//
//  PWM: 50 Hz, 1000–2000 µs
//    1000 µs = pleine marche arrière
//    1500 µs = stop
//    2000 µs = pleine marche avant
//
//  Arming: envoyer ESC_PULSE_MIN_US pendant ESC_ARM_DELAY_MS
//  Le sens des moteurs est géré par le câblage des ESC.
// ============================================================

#include <Arduino.h>
#include <Servo.h>
#include <Config.h>

class ESC {
public:
    ESC() = default;

    /**
     * @brief  Attache les pins PWM et arme les deux ESC.
     *         Bloque pendant ESC_ARM_DELAY_MS.
     */
    void begin();

    /**
     * @brief  Envoie la même commande aux deux moteurs.
     * @param  throttle  [-1.0, +1.0]
     *                   +1.0 = pleine avant, -1.0 = pleine arrière, 0 = stop
     */
    void set(float throttle);

    /**
     * @brief  Stop immédiat — envoie le pulse neutre aux deux ESC.
     */
    void stop();

    /**
     * @brief  Désarme — envoie ESC_PULSE_MIN_US (position sécuritaire).
     */
    void disarm();

    float getThrottle() const { return _throttle; }
    bool  isArmed()     const { return _armed;    }

private:
    Servo _escLeft;
    Servo _escRight;

    float _throttle = 0.0f;
    bool  _armed    = false;

    uint16_t toPulse(float throttle) const;
};