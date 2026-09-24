// ============================================================
//  UGT207.cpp  –  ifm UGT207 ultrasonic sensor driver
//  RC Hydrofoil – Teensy 4.1
// ============================================================

#include <UGT207.h>

// ----------------------------------------------------------------
// begin()
// ----------------------------------------------------------------
void UGT207::begin()
{
    analogReadResolution(10);  // 10-bit ADC

    pinMode(PIN_UGT_ANALOG,  INPUT);
    // pinMode(PIN_UGT207_DIGITAL, INPUT);  // external 10kΩ pull-down

    Serial.println("[UGT207] Initialised OK");
}

// ----------------------------------------------------------------
// update()
// ----------------------------------------------------------------
void UGT207::update()
{
    // --- Analog ---
    uint16_t adc = (uint16_t)analogRead(PIN_UGT_ANALOG);

    // Below 4 mA threshold (5% margin) = no valid signal
    if (adc < (uint16_t)(ADC_AT_4MA * 0.95f)) {
        _analogValid = false;
    } else {
        float rawCm = adcToCm(adc);

        if (rawCm < DIST_MIN_CM || rawCm > DIST_MAX_CM) {
            _analogValid = false;
        } else {
            _rawCm = rawCm;

            if (!_analogValid) {
                _filteredCm = rawCm;  // seed on first valid reading
            } else {
                _filteredCm = (1.0f - UGT_ALPHA) * _filteredCm
                            + UGT_ALPHA * rawCm;
            }
            _analogValid = true;
        }
    }

    // --- Digital ---
    // _detected  = (digitalRead(PIN_UGT_IO) == HIGH);
    _timestamp = micros();
}

// ----------------------------------------------------------------
// adcToCm()  –  10-bit ADC → distance [cm]
// ----------------------------------------------------------------
float UGT207::adcToCm(uint16_t adc) const
{
    float t  = ((float)adc - ADC_AT_4MA) / (ADC_AT_CLIP - ADC_AT_4MA);
    float cm = DIST_MIN_CM + t * (DIST_MAX_CM - DIST_MIN_CM);

    if (cm > DIST_MAX_CM) cm = DIST_MAX_CM;
    if (cm < DIST_MIN_CM) cm = DIST_MIN_CM;

    return cm;
}

// ----------------------------------------------------------------
// printDebug()
// ----------------------------------------------------------------
void UGT207::printDebug() const
{
    if (_analogValid) {
        Serial.printf("[UGT207] Distance: %6.1f cm  (raw: %6.1f cm)  Object: %s\n",
                      _filteredCm, _rawCm,
                      _detected ? "YES" : "no");
        }
    //  else {
    //     Serial.printf("[UGT207] Analog invalid  Object: %s\n",
    //                   _detected ? "YES" : "no");
    // }
}