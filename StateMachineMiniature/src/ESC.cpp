// ============================================================
//  ESC.cpp  –  Dual ESC PWM driver
//  RC Hydrofoil – Teensy 4.1
// ============================================================

#include <ESC.h>


// ----------------------------------------------------------------
// begin()  –  arm sequence
// ----------------------------------------------------------------
void ESC::begin()
{
    _escLeft.attach(PIN_ESC_LEFT);
    _escRight.attach(PIN_ESC_RIGHT);

    // Arming: hold minimum pulse for ESC_ARM_DELAY_MS
    _escLeft.writeMicroseconds(ESC_ARM_PULSE_US);
    _escRight.writeMicroseconds(ESC_ARM_PULSE_US);
    delay(ESC_ARM_DELAY_MS);

    _armed    = true;
    _throttle = 0.0f;

    // Go to neutral after arming
    stop();

    Serial.println("[ESC] Armed OK");
}

// ----------------------------------------------------------------
// set()
// ----------------------------------------------------------------
void ESC::set(float throttle)
{
    if (!_armed) return;

    // Clamp [-1, +1]
    if (throttle >  1.0f) throttle =  1.0f;
    if (throttle < -1.0f) throttle = -1.0f;

    _throttle = throttle;

    uint16_t pulse = toPulse(throttle);
    _escLeft.writeMicroseconds(pulse);
    _escRight.writeMicroseconds(pulse);
}

// ----------------------------------------------------------------
// stop()
// ----------------------------------------------------------------
void ESC::stop()
{
    _throttle = 0.0f;
    _escLeft.writeMicroseconds(ESC_PULSE_NEUTRAL_US);
    _escRight.writeMicroseconds(ESC_PULSE_NEUTRAL_US);
}

// ----------------------------------------------------------------
// disarm()
// ----------------------------------------------------------------
void ESC::disarm()
{
    _throttle = 0.0f;
    _armed    = false;
    _escLeft.writeMicroseconds(ESC_ARM_PULSE_US);
    _escRight.writeMicroseconds(ESC_ARM_PULSE_US);
    Serial.println("[ESC] Disarmed");
}

// ----------------------------------------------------------------
// toPulse()  –  [-1.0, +1.0] → [ESC_PULSE_MIN_US, ESC_PULSE_MAX_US]
// ----------------------------------------------------------------
uint16_t ESC::toPulse(float throttle) const
{
    float pulse = ESC_PULSE_NEUTRAL_US
                + throttle * (float)(ESC_PULSE_MAX_US - ESC_PULSE_NEUTRAL_US);

    if (pulse > ESC_PULSE_MAX_US) pulse = ESC_PULSE_MAX_US;
    if (pulse < ESC_PULSE_MIN_US) pulse = ESC_PULSE_MIN_US;

    return (uint16_t)pulse;
}