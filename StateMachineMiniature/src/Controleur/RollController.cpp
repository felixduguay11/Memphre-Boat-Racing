// ============================================================
//  RollController.cpp  –  PID roulis
//  RC Hydrofoil – Teensy 4.1
// ============================================================

#include <RollController.h>

void RollController::begin()
{
    reset();
    Serial.println("[Roll] Controller initialised OK");
}

void RollController::reset()
{
    _integral  = 0.0f;
    _prevError = 0.0f;
    _error     = 0.0f;
    _output    = 0.0f;
}

// ----------------------------------------------------------------
// update()
// ----------------------------------------------------------------
float RollController::update(float rollDeg, float dt)
{
    if (dt <= 0.0f) return _output;

    // Error: positive = rolled right (right wing low)
    _error = ROLL_SETPOINT_DEG - rollDeg;

    // Integral with anti-windup clamp
    _integral += _error * dt;
    if (_integral >  ROLL_I_MAX) _integral =  ROLL_I_MAX;
    if (_integral < -ROLL_I_MAX) _integral = -ROLL_I_MAX;

    // Derivative
    float derivative = (_error - _prevError) / dt;
    _prevError = _error;

    // PID output [deg]
    _output = ROLL_KP * _error
            + ROLL_KI * _integral
            + ROLL_KD * derivative;

    // Clamp to half the foil differential travel
    float maxOut = (FOIL_ANGLE_MAX_DEG - FOIL_ANGLE_NEUTRAL) / 2.0f;
    if (_output >  maxOut) _output =  maxOut;
    if (_output < -maxOut) _output = -maxOut;

    return _output;
}

// ----------------------------------------------------------------
// printDebug()
// ----------------------------------------------------------------
void RollController::printDebug() const
{
    Serial.printf("[Roll]   Setpoint: %5.1f deg  Error: %6.2f deg  Output: %6.2f deg\n",
                  ROLL_SETPOINT_DEG, _error, _output);
}