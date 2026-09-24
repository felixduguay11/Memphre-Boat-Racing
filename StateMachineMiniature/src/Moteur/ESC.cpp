// ============================================================
//  ESC.cpp  –  Dual ESC PWM driver
//  RC Hydrofoil – Teensy 4.1
// ============================================================

#include <ESC.h>

// Convert [-1.0, +1.0] → degrees (0–180) for PWMServo
// Maps: -1 → 0°, 0 → 90°, +1 → 180°
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

    // Arming: hold minimum (0°) for ESC_ARM_DELAY_MS
    _escLeft.write(0);
    _escRight.write(0);
    delay(ESC_ARM_DELAY_MS);

    _armed    = true;
    _throttle = 0.0f;

    stop();
    Serial.println("[ESC] Armed OK");
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
    _escLeft.write(90);   // 90° = neutral = 1500 µs
    _escRight.write(90);
}

void ESC::disarm()
{
    _throttle = 0.0f;
    _armed    = false;
    _escLeft.write(0);
    _escRight.write(0);
    Serial.println("[ESC] Disarmed");
}

void ESC::printDebug() const
{
    Serial.printf("[ESC]   Throttle: %5.2f\n", getThrottle());
}