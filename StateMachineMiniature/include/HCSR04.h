#pragma once

// ============================================================
//  Sonar.h  –  HC-SR04 ultrasonic sensor driver  (OOP, ISR-driven)
//  RC Hydrofoil – Teensy 4.1
//
//  Wiring:
//    TRIG → PIN_SONAR_TRIG  (Config.h, default 14)
//    ECHO → PIN_SONAR_ECHO  (Config.h, default 15)  via 1kΩ/2kΩ divider
//    VCC  → 5 V
//    GND  → GND (common with Teensy)
//
//  Call order each cycle:
//    1. update()   ← consume last echo first
//    2. trigger()  ← then fire next pulse
// ============================================================

#include <Arduino.h>
#include <Config.h>

class Sonar {
public:
    explicit Sonar(uint8_t trigPin = PIN_SONAR_TRIG,
                   uint8_t echoPin = PIN_SONAR_ECHO);

    void begin();
    void trigger();   // fire pulse, non-blocking — call AFTER update()
    bool update();    // consume ISR data   — call BEFORE trigger()

    float    getDistanceCm()  const { return _filteredCm;  }
    float    getRawCm()       const { return _rawCm;       }
    bool     isValid()        const { return _valid;       }
    uint32_t getTimestamp()   const { return _timestamp;   }

    // ISR callback — public for the wrapper, do not call manually
    void echoISR();

private:
    uint8_t  _trigPin;
    uint8_t  _echoPin;

    volatile uint32_t _echoStart = 0;
    volatile uint32_t _echoEnd   = 0;
    volatile bool     _echoReady = false;

    float    _rawCm      = 0.0f;
    float    _filteredCm = 0.0f;
    bool     _valid      = false;
    uint32_t _timestamp  = 0;

    static constexpr float CM_PER_US = 0.0343f / 2.0f;
};