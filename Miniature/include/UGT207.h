#ifndef UGT207_H
#define UGT207_H

// ============================================================
//  UGT207.h  –  ifm UGT207 ultrasonic sensor driver
//  RC Hydrofoil – Teensy 4.1
//
//  Wiring:
//    Pin 1 (L+)  → 15V external supply
//    Pin 3 (L−)  → GND (common with Teensy)
//    Pin 2 (Ana) → 250Ω shunt → 1kΩ/2kΩ divider → PIN_UGT207_ANALOG
//    Pin 4 (Dig) → 10kΩ pull-down to GND          → PIN_UGT207_DIGITAL
//
//  Analog: 4–20 mA current loop
//    250Ω shunt → 1.0–5.0V
//    1kΩ/2kΩ divider → 0.667–3.33V  (clips at 3.3V = ~19.8 mA = ~2175 mm)
//    10-bit ADC → 0–1023
//    ADC 207 → 20 cm  (4 mA)
//    ADC 1023 → ~217.5 cm  (3.3V clip)
//
//  Digital: PNP normally open — HIGH = object detected
//           Switching frequency: 2 Hz max
//
//  ⚠  Response time analog: < 300 ms
//  ⚠  Blind zone: 20 cm
//  ⚠  Best accuracy after 20 min warm-up
// ============================================================

#include <Arduino.h>
#include <Config.h>

class UGT207 {
public:
    UGT207() = default;

    void begin();
    void update();  // call at UGT207_UPDATE_HZ

    // Analog — distance in cm
    float    getDistanceCm()  const { return _filteredCm;  }
    float    getRawCm()       const { return _rawCm;        }
    bool     isAnalogValid()  const { return _analogValid;  }

    // Digital — object detected
    bool     objectDetected() const { return _detected;     }

    uint32_t getTimestamp()   const { return _timestamp;    }

    void printDebug() const;

private:
    float    _rawCm       = 0.0f;
    float    _filteredCm  = 0.0f;
    bool     _analogValid = false;
    bool     _detected    = false;
    uint32_t _timestamp   = 0;

    // 10-bit ADC mapping
    // 4 mA  → 0.667V → ADC 207  → 20 cm
    // ~19.8mA → 3.3V  → ADC 1023 → ~217.5 cm
    static constexpr float ADC_AT_4MA  = 186.0f;
    static constexpr float ADC_AT_CLIP = 930.0f;
    static constexpr float DIST_MIN_CM  = 20.0f;
    static constexpr float DIST_MAX_CM  = 217.5f;

    float adcToCm(uint16_t adc) const;
};
#endif
