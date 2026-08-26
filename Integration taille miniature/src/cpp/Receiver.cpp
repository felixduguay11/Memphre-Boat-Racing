// ============================================================
//  Receiver.cpp  –  Récepteur FlySky PWM (5 canaux)
//  Memphre Boat Racing – Teensy 4.1
//
//  Porté depuis Miniature/src/Capteur/Receiver.cpp (fidèle).
// ============================================================

#include "Receiver.h"

// ----------------------------------------------------------------
// Instance globale + un wrapper ISR par canal
// ----------------------------------------------------------------
static RCReceiver* _rcInstance = nullptr;

static void isr_ch0() { if (_rcInstance) _rcInstance->onEdge(0); }
static void isr_ch1() { if (_rcInstance) _rcInstance->onEdge(1); }
static void isr_ch2() { if (_rcInstance) _rcInstance->onEdge(2); }
static void isr_ch3() { if (_rcInstance) _rcInstance->onEdge(3); }
static void isr_ch4() { if (_rcInstance) _rcInstance->onEdge(4); }

static void (*const ISR_TABLE[RC_NUM_CHANNELS])() = {
    isr_ch0, isr_ch1, isr_ch2, isr_ch3, isr_ch4
};

constexpr uint8_t RCReceiver::PINS[RC_NUM_CHANNELS];

// ----------------------------------------------------------------
// begin()
// ----------------------------------------------------------------
void RCReceiver::begin()
{
    _rcInstance = this;

    for (uint8_t i = 0; i < RC_NUM_CHANNELS; i++) {
        pinMode(PINS[i], INPUT);
        attachInterrupt(digitalPinToInterrupt(PINS[i]), ISR_TABLE[i], CHANGE);
    }

    Serial.println("[RC] Recepteur FlySky initialise OK");
}

// ----------------------------------------------------------------
// onEdge()  –  corps de l'ISR
// ----------------------------------------------------------------
void RCReceiver::onEdge(uint8_t ch)
{
    if (digitalReadFast(PINS[ch]) == HIGH) {
        _riseTime[ch] = micros();
    } else {
        uint32_t pulse = micros() - _riseTime[ch];
        if (pulse >= 500 && pulse <= 2500) {
            _pulseUs[ch] = (uint16_t)pulse;
            _newData[ch] = true;
        }
    }
}

// ----------------------------------------------------------------
// update()  –  consomme les captures ISR, valide, normalise
// ----------------------------------------------------------------
void RCReceiver::update()
{
    bool anyFresh = false;
    bool allValid = true;

    for (uint8_t i = 0; i < RC_NUM_CHANNELS; i++) {
        noInterrupts();
        bool     fresh = _newData[i];
        uint16_t pulse = _pulseUs[i];
        _newData[i]    = false;  // consomme
        interrupts();

        if (fresh) {
            _data.pulseUs[i] = pulse;
            // Throttle (index 1 = CH3) borné à [0,+1], le reste [-1,+1]
            bool positiveOnly = (i == 1);
            _data.channel[i]  = normalise(pulse, positiveOnly);
            anyFresh = true;
        }

        if (_data.pulseUs[i] == 0) allValid = false;
    }

    if (anyFresh) {
        _data.valid     = allValid;
        _data.timestamp = micros();
    } else {
        // Aucune impulsion fraîche — vérifier le timeout
        uint32_t elapsedMs = (micros() - _data.timestamp) / 1000;
        if (elapsedMs > RC_TIMEOUT_MS) {
            _data.valid = false;
        }
    }
}

// ----------------------------------------------------------------
// normalise()
//   clampPositive = true  → throttle : [0.0, +1.0]
//   clampPositive = false → rudder/sw : [-1.0, +1.0]
// ----------------------------------------------------------------
float RCReceiver::normalise(uint16_t pulseUs, bool clampPositive) const
{
    float centred = (float)pulseUs - RC_PULSE_MID_US;

    // Deadband
    if (centred > -(float)RC_DEADBAND_US && centred < (float)RC_DEADBAND_US) {
        centred = 0.0f;
    }

    float half = (float)(RC_PULSE_MAX_US - RC_PULSE_MID_US);
    float norm = centred / half;

    if (norm >  1.0f) norm =  1.0f;
    if (norm < -1.0f) norm = -1.0f;

    if (clampPositive) {
        // Remap [-1,+1] → [0,+1] pour le throttle
        norm = (norm + 1.0f) / 2.0f;
    }

    return norm;
}

// ----------------------------------------------------------------
// getPulseUs()
// ----------------------------------------------------------------
uint16_t RCReceiver::getPulseUs(uint8_t ch) const
{
    if (ch >= RC_NUM_CHANNELS) return RC_PULSE_MID_US;
    return _data.pulseUs[ch];
}

void RCReceiver::printDebug() const
{
    Serial.printf("[RC]    Thr: %5.2f  Rud: %5.2f  SwA:%d Knob:%5.2f SwC:%d\n",
                  throttle(), rudder(),
                  switchA(), knobHeight(), switchC());
}
