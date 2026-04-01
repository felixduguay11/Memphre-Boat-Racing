// ============================================================
//  Sonar.cpp  –  HC-SR04 driver  (OOP, ISR-driven)
//  RC Hydrofoil – Teensy 4.1
// ============================================================

#include "HCSR04.h"

// ----------------------------------------------------------------
// Global instance pointer for the ISR wrapper (one sonar on boat)
// ----------------------------------------------------------------
static Sonar* _instance = nullptr;

static void echoISR_wrapper() {
    if (_instance) _instance->echoISR();
}

// ----------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------
Sonar::Sonar(uint8_t trigPin, uint8_t echoPin)
    : _trigPin(trigPin), _echoPin(echoPin)
{}

// ----------------------------------------------------------------
// begin()
// ----------------------------------------------------------------
void Sonar::begin()
{
    pinMode(_trigPin, OUTPUT);
    digitalWriteFast(_trigPin, LOW);
    pinMode(_echoPin, INPUT);

    _instance = this;
    attachInterrupt(digitalPinToInterrupt(_echoPin), echoISR_wrapper, CHANGE);

    Serial.println("[Sonar] HC-SR04 initialised OK");
}

// ----------------------------------------------------------------
// update()  –  call BEFORE trigger()
// ----------------------------------------------------------------
bool Sonar::update()
{
    noInterrupts();
    bool     ready = _echoReady;
    uint32_t start = _echoStart;
    uint32_t end   = _echoEnd;
    interrupts();

    if (!ready) {
        _valid = false;
        return false;
    }

    float rawCm = (float)(end - start) * CM_PER_US;

    if (rawCm < SONAR_MIN_CM || rawCm > SONAR_MAX_CM) {
        _valid = false;
        return false;
    }

    _rawCm = rawCm;

    if (!_valid) {
        _filteredCm = rawCm;  // seed on first valid reading
    } else {
        _filteredCm = (1.0f - SONAR_FILTER_ALPHA) * _filteredCm
                    + SONAR_FILTER_ALPHA * rawCm;
    }

    _valid     = true;
    _timestamp = micros();
    return true;
}

// ----------------------------------------------------------------
// trigger()  –  call AFTER update()
// ----------------------------------------------------------------
void Sonar::trigger()
{
    // Safe to reset now — update() already consumed the last echo
    noInterrupts();
    _echoReady = false;
    _echoStart = 0;
    _echoEnd   = 0;
    interrupts();

    digitalWriteFast(_trigPin, HIGH);
    delayMicroseconds(10);
    digitalWriteFast(_trigPin, LOW);
}

// ----------------------------------------------------------------
// echoISR()
// ----------------------------------------------------------------
void Sonar::echoISR()
{
    if (digitalReadFast(_echoPin) == HIGH) {
        _echoStart = micros();
    } else {
        _echoEnd   = micros();
        _echoReady = true;
    }
}

void Sonar::printDebug() const
{
    Serial.printf("[Sonar] Distance: %6.1f cm\n", _filteredCm);
}
