// ============================================================
//  HeightController.cpp  –  PID hauteur
//  RC Hydrofoil – Teensy 4.1
// ============================================================

#include <HeightController.h>

void HeightController::begin()
{
    reset();
    Serial.println("[Height] Controller initialised OK");
}

void HeightController::reset()
{
    _integral  = 0.0f;
    _prevError = 0.0f;
    _error     = 0.0f;
    _output    = 0.0f;
}

// ----------------------------------------------------------------
// update()
// ----------------------------------------------------------------
float HeightController::update(float measuredCm, float knob, float dt)
{
    if (dt <= 0.0f) return _output;

    // Update setpoint from knob
    updateSetpoint(knob);

    // Error: positive = too low (need more lift)
    _error = _setpoint - measuredCm;

    // Integral with anti-windup clamp
    _integral += _error * dt;
    if (_integral >  HEIGHT_I_MAX) _integral =  HEIGHT_I_MAX;
    if (_integral < -HEIGHT_I_MAX) _integral = -HEIGHT_I_MAX;

    // Derivative
    float derivative = (_error - _prevError) / dt;
    _prevError = _error;

    // PID output [deg]
    _output = HEIGHT_KP * _error
            + HEIGHT_KI * _integral
            + HEIGHT_KD * derivative;

    // Clamp output to foil travel range around neutral
    float maxOut = FOIL_ANGLE_MAX_DEG - FOIL_ANGLE_NEUTRAL;
    float minOut = FOIL_ANGLE_MIN_DEG - FOIL_ANGLE_NEUTRAL;
    if (_output >  maxOut) _output =  maxOut;
    if (_output <  minOut) _output =  minOut;

    return _output;
}

// ----------------------------------------------------------------
// updateSetpoint()  –  knob [-1,+1] → [MIN_CM, MAX_CM]
// ----------------------------------------------------------------
void HeightController::updateSetpoint(float knob)
{
#if HEIGHT_SETPOINT_FIXED
    _setpoint = HEIGHT_SETPOINT_DEFAULT_CM;
#else
    float t = (knob + 1.0f) / 2.0f;
    _setpoint = HEIGHT_SETPOINT_MIN_CM
              + t * (HEIGHT_SETPOINT_MAX_CM - HEIGHT_SETPOINT_MIN_CM);
#endif
}
// ----------------------------------------------------------------
// printDebug()
// ----------------------------------------------------------------
void HeightController::printDebug() const
{
    Serial.printf("[Height] Setpoint: %5.1f cm  Error: %6.2f cm  Output: %6.2f deg\n",
                  _setpoint, _error, _output);
}