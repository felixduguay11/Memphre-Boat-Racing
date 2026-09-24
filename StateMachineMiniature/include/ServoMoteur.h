#pragma once

// ============================================================
//  Servo.h  –  Rudder + 3 Hydrofoil servos
//  RC Hydrofoil – Teensy 4.1
//
//  Wiring (from Config.h):
//    PIN_SERVO_RUDDER    → Servo direction  (pin 4)
//    PIN_FOIL_FRONT      → Hydrofoil avant  (pin 5)
//    PIN_FOIL_REAR_LEFT  → Hydrofoil AR gauche (pin 6)
//    PIN_FOIL_REAR_RIGHT → Hydrofoil AR droit  (pin 7)
//
//  Tous les servos: 50 Hz, 500–2500 µs, course 0–180°
//    500  µs = 0°   (position min)
//    1500 µs = 90°  (centre / neutre)
//    2500 µs = 180° (position max)
// ============================================================

#include <Arduino.h>
#include <PWMServo.h>
#include <Config.h>

// ================================================================
//  ServoRudder  –  servo de direction
// ================================================================
class ServoRudder {
public:
    ServoRudder() = default;

    void begin();

    /**
     * @brief  Commande le rudder.
     * @param  cmd  [-1.0, +1.0]  -1 = gauche max, +1 = droite max
     */
    void set(float cmd);

    void center();  // retour au centre (0°)

    float    getCmd()     const { return _cmd;     }
    uint16_t getPulseUs() const { return _pulseUs; }

    void printDebug() const;

private:
    PWMServo    _servo;
    float    _cmd     = 0.0f;
    uint16_t _pulseUs = RUDDER_CENTER_US;

    uint16_t toPulse(float cmd) const;
};

// ================================================================
//  ServoFoils  –  3 servos hydrofoils
// ================================================================
class ServoFoils {
public:
    ServoFoils() = default;

    void begin();

    /**
     * @brief  Commande identique sur les 3 foils (mode fixe).
     * @param  angle  [FOIL_ANGLE_MIN_DEG, FOIL_ANGLE_MAX_DEG]
     */
    void setAll(float angleDeg);

    /**
     * @brief  Commandes indépendantes pour le contrôle hauteur + roulis.
     * @param  front      angle foil avant   [deg]
     * @param  rearLeft   angle foil AR gauche [deg]
     * @param  rearRight  angle foil AR droit  [deg]
     */
    void set(float front, float rearLeft, float rearRight);

    void neutral();  // tous au FOIL_ANGLE_NEUTRAL

    float getFront()     const { return _front;     }
    float getRearLeft()  const { return _rearLeft;  }
    float getRearRight() const { return _rearRight; }

    void printDebug() const;
private:
    PWMServo _servoFront;
    PWMServo _servoRearLeft;
    PWMServo _servoRearRight;

    float _front     = FOIL_ANGLE_NEUTRAL;
    float _rearLeft  = FOIL_ANGLE_NEUTRAL;
    float _rearRight = FOIL_ANGLE_NEUTRAL;

    uint16_t toPulse(float angleDeg) const;
    float    clamp(float angle) const;
};