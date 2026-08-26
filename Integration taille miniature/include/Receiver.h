#pragma once

// ============================================================
//  Receiver.h  –  Récepteur FlySky PWM (5 canaux)
//  Memphre Boat Racing – Teensy 4.1
//
//  Porté depuis Miniature/include/Receiver.h.
//  Seule modif vs original : include "config.h" (minuscule, guillemets)
//  pour coller au projet "Integration taille miniature".
//
//  Canaux utilisés :
//    CH1 → PIN_RC_CH1  Rudder (direction)   → channel[0]  [-1,+1]
//    CH3 → PIN_RC_CH3  Throttle             → channel[1]  [ 0,+1]
//    CH5 → PIN_RC_CH5  Switch A (armement)  → channel[2]  bool
//    CH6 → PIN_RC_CH6  Knob VrA (hauteur)   → channel[3]  [-1,+1]
//    CH7 → PIN_RC_CH7  Switch C (marche AR) → channel[4]  bool
//
//  PWM : 1000–2000 µs à ~50 Hz. Interruption CHANGE par canal.
//  update() valide, applique la deadband, normalise en [-1,+1].
//
//  ⚠ Si le récepteur sort du 5 V, mettre un diviseur 1k/2k sur chaque pin.
// ============================================================

#include <Arduino.h>
#include "config.h"

static constexpr uint8_t RC_NUM_CHANNELS = 5;

struct RCData {
    float    channel[RC_NUM_CHANNELS];  // normalisé [-1.0, +1.0]
    uint16_t pulseUs[RC_NUM_CHANNELS];  // largeur brute [µs]
    bool     valid;                     // false si signal perdu / timeout
    uint32_t timestamp;                 // micros() de la dernière update()
};

class RCReceiver {
public:
    RCReceiver() = default;

    void begin();
    void update();  // appeler à ~50 Hz ou plus vite

    bool     isValid()      const { return _data.valid;     }
    uint32_t getTimestamp() const { return _data.timestamp; }
    const RCData& getData() const { return _data;           }

    // Accesseurs nommés
    float    rudder()     const { return _data.channel[0]; }        // CH1 [-1,+1]
    float    throttle()   const { return _data.channel[1]; }        // CH3 [ 0,+1]
    bool     switchA()    const { return _data.channel[2] > 0.5f; } // CH5
    float    knobHeight() const { return _data.channel[3]; }        // CH6 [-1,+1]
    bool     switchC()    const { return _data.channel[4] > 0.5f; } // CH7

    uint16_t getPulseUs(uint8_t ch) const;

    // Callback ISR — ne pas appeler manuellement
    void onEdge(uint8_t ch);

    void printDebug() const;

private:
    volatile uint32_t _riseTime[RC_NUM_CHANNELS] = {};
    volatile uint16_t _pulseUs[RC_NUM_CHANNELS]  = {};
    volatile bool     _newData[RC_NUM_CHANNELS]  = {};

    RCData _data = {};

    // Pins physiques de chacun des 5 canaux
    static constexpr uint8_t PINS[RC_NUM_CHANNELS] = {
        PIN_RC_CH1,   // Rudder
        PIN_RC_CH3,   // Throttle
        PIN_RC_CH5,   // Switch A
        PIN_RC_CH6,   // Knob VrA
        PIN_RC_CH7    // Switch C
    };

    float normalise(uint16_t pulseUs, bool clampPositive) const;
};
