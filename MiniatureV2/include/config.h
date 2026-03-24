#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

/*
 * ADC - UGT207
 * (4mA - 20mA -> sur une plage de 0.4 - 2.0V)
 */

// ── Pins ADC ──────────────────────────────────────────────
#define PIN_SONAR_FRONT     A0
#define PIN_SONAR_REAR_L    A1
#define PIN_SONAR_REAR_R    A2
 
// ── Shunt 4–20 mA ─────────────────────────────────────────
#define SHUNT_OHM           100.0f   // résistance shunt en Ω
 
// ── Plage du capteur UGT207 ────────────────────────────────
#define SENSOR_DIST_MIN_MM  200.0f     // distance à 4 mA
#define SENSOR_DIST_MAX_MM  2200.0f   // distance à 20 mA
 
// ── ADC Teensy 4.1 ─────────────────────────────────────────
#define ADC_BITS            12       // résolution 12-bit
#define ADC_VREF            3.3f     // tension de référenc





#endif