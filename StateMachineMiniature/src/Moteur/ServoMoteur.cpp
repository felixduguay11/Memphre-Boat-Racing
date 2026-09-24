// ============================================================
//  Servo.cpp  –  Rudder + 3 Hydrofoil servos
//  RC Hydrofoil – Teensy 4.1
// ============================================================

#include <ServoMoteur.h>

// Convert µs → degrees for PWMServo
// 500 µs → 0°,  1500 µs → 90°,  2500 µs → 180°
static uint8_t usToDeg(uint16_t us)
{
    float deg = (float)(us - 500) / (2500.0f - 500.0f) * 180.0f;
    if (deg > 180.0f) deg = 180.0f;
    if (deg <   0.0f) deg =   0.0f;
    return (uint8_t)deg;
}

// ================================================================
//  ServoRudder
// ================================================================

void ServoRudder::begin()
{
    _servo.attach(PIN_SERVO_RUDDER);
    center();
    Serial.println("[Servo] Rudder initialised OK");
}

void ServoRudder::set(float cmd)
{
    if (cmd >  1.0f) cmd =  1.0f;
    if (cmd < -1.0f) cmd = -1.0f;

    _cmd     = cmd;
    _pulseUs = toPulse(cmd);
    _servo.write(usToDeg(_pulseUs));
}

void ServoRudder::center()
{
    _cmd     = 0.0f;
    _pulseUs = RUDDER_CENTER_US;
    _servo.write(usToDeg(RUDDER_CENTER_US));
}

uint16_t ServoRudder::toPulse(float cmd) const
{
    float pulse = RUDDER_CENTER_US
                + cmd * (float)(RUDDER_PULSE_MAX_US - RUDDER_CENTER_US);
    if (pulse > RUDDER_PULSE_MAX_US) pulse = RUDDER_PULSE_MAX_US;
    if (pulse < RUDDER_PULSE_MIN_US) pulse = RUDDER_PULSE_MIN_US;
    return (uint16_t)pulse;
}

void ServoRudder::printDebug() const
{
            Serial.printf("[Servo] Rudder: %5.2f",
                      getCmd());
}


// ================================================================
//  ServoFoils
// ================================================================

void ServoFoils::begin()
{
    _servoFront.attach(PIN_FOIL_FRONT);
    _servoRearLeft.attach(PIN_FOIL_REAR_LEFT);
    _servoRearRight.attach(PIN_FOIL_REAR_RIGHT);
    neutral();
    Serial.println("[Servo] Foils initialised OK");
}

void ServoFoils::setAll(float angleDeg)
{
    set(angleDeg, angleDeg, angleDeg);
}

void ServoFoils::set(float front, float rearLeft, float rearRight)
{
    _front     = clamp(front);
    _rearLeft  = clamp(rearLeft);
    _rearRight = clamp(rearRight);

    _servoFront.write(usToDeg(toPulse(_front)));
    _servoRearLeft.write(usToDeg(toPulse(_rearLeft)));
    _servoRearRight.write(usToDeg(toPulse(_rearRight)));
}

void ServoFoils::neutral()
{
    setAll(FOIL_ANGLE_NEUTRAL);
}

uint16_t ServoFoils::toPulse(float angleDeg) const
{
    float t = (angleDeg - FOIL_MIN_DEG) / (float)(FOIL_MAX_DEG - FOIL_MIN_DEG);
    float pulse = FOIL_PULSE_MIN_US + t * (float)(FOIL_PULSE_MAX_US - FOIL_PULSE_MIN_US);
    if (pulse > FOIL_PULSE_MAX_US) pulse = FOIL_PULSE_MAX_US;
    if (pulse < FOIL_PULSE_MIN_US) pulse = FOIL_PULSE_MIN_US;
    return (uint16_t)pulse;
}

float ServoFoils::clamp(float angle) const
{
    if (angle > FOIL_ANGLE_MAX_DEG) return FOIL_ANGLE_MAX_DEG;
    if (angle < FOIL_ANGLE_MIN_DEG) return FOIL_ANGLE_MIN_DEG;
    return angle;
}

void ServoFoils::printDebug() const 
{
            Serial.printf("[Servo] Foils: %.1f / %.1f / %.1f deg\n",
                      getFront(), getRearLeft(), getRearRight());
}
