#pragma once

// ============================================================
//  RCReceiver.h  –  FlySky PWM receiver  (5 channels)
//  RC Hydrofoil – Teensy 4.1
//
//  Channels used:
//    CH1 → PIN_RC_CH1  Rudder (direction)
//    CH3 → PIN_RC_CH3  Throttle
//    CH5 → PIN_RC_CH5  Switch A
//    CH6 → PIN_RC_CH6  Switch B
//    CH7 → PIN_RC_CH7  Switch C
//
//  PWM pulse: 1000–2000 µs at ~50 Hz.
//  Strategy: CHANGE interrupt per channel.
//  update() validates, applies deadband, normalises to [-1, +1].
//
//  ⚠  If receiver outputs 5 V, use 1kΩ/2kΩ divider on each pin.
// ============================================================

#include <Arduino.h>
#include <config.h>

static constexpr uint8_t RC_NUM_CHANNELS = 5;

struct RCData {
    float    channel[RC_NUM_CHANNELS];  // normalised [-1.0, +1.0]
    uint16_t pulseUs[RC_NUM_CHANNELS];  // raw pulse width [µs]
    bool     valid;                     // false if signal lost/timeout
    uint32_t timestamp;                 // micros() of last update()
};

class RCReceiver {
public:
    RCReceiver() = default;

    void begin();
    void update();  // call at ~50 Hz or faster

    bool     isValid()      const { return _data.valid;     }
    uint32_t getTimestamp() const { return _data.timestamp; }
    const RCData& getData() const { return _data;           }

    // Named accessors
    float    rudder()   const { return _data.channel[0]; }  // CH1 [-1, +1]
    float    throttle() const { return _data.channel[1]; }  // CH3 [ 0, +1]
    bool     switchA()  const { return _data.channel[2] > 0.5f; }  // CH5
    float    knobHeight() const { return _data.channel[3]; }       // CH6 [-1,+1]
    bool     switchC()  const { return _data.channel[4] > 0.5f; }  // CH7

    uint16_t getPulseUs(uint8_t ch) const;

    // ISR callback — do not call manually
    void onEdge(uint8_t ch);

    void printDebug() const;

private:
    volatile uint32_t _riseTime[RC_NUM_CHANNELS] = {};
    volatile uint16_t _pulseUs[RC_NUM_CHANNELS]  = {};
    volatile bool     _newData[RC_NUM_CHANNELS]  = {};

    RCData _data = {};

    // Physical pins for each of the 5 channels
    static constexpr uint8_t PINS[RC_NUM_CHANNELS] = {
        PIN_RC_CH1,   // Rudder
        PIN_RC_CH3,   // Throttle
        PIN_RC_CH5,   // Switch A
        PIN_RC_CH6,   // Switch B
        PIN_RC_CH7    // Switch C
    };

    float normalise(uint16_t pulseUs, bool clampPositive) const;
};