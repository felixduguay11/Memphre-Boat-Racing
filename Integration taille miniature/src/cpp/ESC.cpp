// ============================================================
//  ESC.cpp  –  Pilote double ESC (PWM)
//  Memphre Boat Racing – Teensy 4.1
//
//  Porté depuis Miniature/src/Moteur/ESC.cpp.
//  MODIF PORTAGE FreeRTOS : le delay(ESC_ARM_DELAY_MS) bloquant de
//  l'original est remplacé par vTaskDelay(...) afin de rendre la main
//  au scheduler pendant les 2 s d'armement (les autres tâches
//  continuent de tourner). C'est pourquoi ce .cpp inclut
//  <arduino_freertos.h>.
// ============================================================

#include "ESC.h"
#include <arduino_freertos.h>

// Convert [-1.0,+1.0] → degrés (0–180) pour PWMServo
// Map : -1 → 0°, 0 → 90°, +1 → 180°
static uint8_t toDeg(float throttle)
{
    float deg = 90.0f + throttle * 90.0f;
    if (deg > 180.0f) deg = 180.0f;
    if (deg <   0.0f) deg =   0.0f;
    return (uint8_t)deg;
}

void ESC::begin()
{
    _escLeft.attach(PIN_ESC_LEFT);
    _escRight.attach(PIN_ESC_RIGHT);

    // Armement : maintenir le minimum (0°) pendant ESC_ARM_DELAY_MS.
    _escLeft.write(0);
    _escRight.write(0);
    vTaskDelay(pdMS_TO_TICKS(ESC_ARM_DELAY_MS));   // ← était delay() dans le miniature

    _armed    = true;
    _throttle = 0.0f;

    stop();
    Serial.println("[ESC] Arme OK");
}

void ESC::set(float throttle)
{
    if (!_armed) return;
    if (throttle >  1.0f) throttle =  1.0f;
    if (throttle < -1.0f) throttle = -1.0f;

    _throttle = throttle;
    uint8_t deg = toDeg(throttle);
    _escLeft.write(deg);
    _escRight.write(deg);
}

void ESC::stop()
{
    _throttle = 0.0f;
    _escLeft.write(90);   // 90° = neutre ≈ 1500 µs
    _escRight.write(90);
}

void ESC::disarm()
{
    _throttle = 0.0f;
    _armed    = false;
    _escLeft.write(0);
    _escRight.write(0);
    Serial.println("[ESC] Desarme");
}

void ESC::printDebug() const
{
    Serial.printf("[ESC]   Throttle: %5.2f\n", getThrottle());
}
