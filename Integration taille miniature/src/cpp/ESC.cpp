// ============================================================
//  ESC.cpp  –  Pilote double ESC (PWM)
//  Memphre Boat Racing – Teensy 4.1
//
//  Porté depuis Miniature/src/Moteur/ESC.cpp.
//  MODIF PORTAGE FreeRTOS : le delay(ESC_ARM_DELAY_MS) bloquant de
//  l'original est remplacé par vTaskDelay(...) afin de rendre la main
//  au scheduler pendant les 2 s d'armement (les autres tâches
//  continuent de tourner). C'est pourquoi ce .cpp inclut
//  <arduino_freertos.h>.
//
//  MODIF CALIBRATION (voir aussi config.h) :
//   1) attach() avec bornes explicites 1000–2000 µs. Avant, attach(pin)
//      seul laissait PWMServo sur ses defauts 544–2400 µs : le neutre
//      tombait a 1472 µs et le stick saturait vers 57 %.
//   2) Armement et desarmement au NEUTRE (90 deg = 1500 µs) : les ESC
//      sont reversibles, 1000 µs = pleine marche arriere.
//   3) toDeg() calcule en µs et applique un trim par moteur
//      (gain + offset) pour compenser l'ecart de vitesse gauche/droite.
// ============================================================

#include "ESC.h"
#include <arduino_freertos.h>

// ── Valeurs de repli si elles ne sont pas encore dans config.h ──
#ifndef ESC_L_GAIN
  #define ESC_L_GAIN        1.0f    // 1.0 = aucune correction
  #define ESC_L_OFFSET_US   0.0f
  #define ESC_R_GAIN        1.0f
  #define ESC_R_OFFSET_US   0.0f
  #define ESC_TRIM_THR_MIN  0.02f   // sous ce seuil : neutre exact
#endif

// Convert [-1.0,+1.0] → µs (avec trim) → degrés (0–180) pour PWMServo
// Map : 0 deg = ESC_PULSE_MIN_US, 90 deg = neutre, 180 deg = ESC_PULSE_MAX_US
static uint8_t toDeg(float throttle, float gain, float offset_us)
{
    float us = (float)ESC_PULSE_NEUTRAL_US
             + throttle * gain * (float)(ESC_PULSE_MAX_US - ESC_PULSE_NEUTRAL_US);

    // Offset applique seulement si le moteur tourne : le neutre reste exact.
    if (fabsf(throttle) > ESC_TRIM_THR_MIN)
        us += (throttle > 0.0f) ? offset_us : -offset_us;

    if (us > (float)ESC_PULSE_MAX_US) us = (float)ESC_PULSE_MAX_US;
    if (us < (float)ESC_PULSE_MIN_US) us = (float)ESC_PULSE_MIN_US;

    float deg = (us - (float)ESC_PULSE_MIN_US) * 180.0f
              / (float)(ESC_PULSE_MAX_US - ESC_PULSE_MIN_US);

    if (deg > 180.0f) deg = 180.0f;
    if (deg <   0.0f) deg =   0.0f;
    return (uint8_t)lroundf(deg);   // arrondi, pas troncature
}

void ESC::begin()
{
    _escLeft.attach (PIN_ESC_LEFT,  ESC_PULSE_MIN_US, ESC_PULSE_MAX_US);
    _escRight.attach(PIN_ESC_RIGHT, ESC_PULSE_MIN_US, ESC_PULSE_MAX_US);

    // Armement : maintenir le NEUTRE (90 deg = 1500 µs) pendant
    // ESC_ARM_DELAY_MS. ESC reversibles → surtout pas 1000 µs ici.
    _escLeft.write(90);
    _escRight.write(90);
    vTaskDelay(pdMS_TO_TICKS(ESC_ARM_DELAY_MS));   // ← était delay() dans le miniature

    _armed    = true;
    _throttle = 0.0f;

    stop();
    Serial.println("[ESC] Arme OK");
}

void ESC::set(float throttle)
{
    if (!_armed) return;
    if (throttle >  1.0f) throttle =  1.0f;
    if (throttle < -1.0f) throttle = -1.0f;

    _throttle = throttle;
    _escLeft.write (toDeg(throttle, ESC_L_GAIN, ESC_L_OFFSET_US));
    _escRight.write(toDeg(throttle, ESC_R_GAIN, ESC_R_OFFSET_US));
}

void ESC::stop()
{
    _throttle = 0.0f;
    _escLeft.write(90);   // 90° = neutre = 1500 µs
    _escRight.write(90);
}

void ESC::disarm()
{
    _throttle = 0.0f;
    _armed    = false;
    _escLeft.write(90);   // était 0 → donnerait 1000 µs = pleine marche AR
    _escRight.write(90);
    Serial.println("[ESC] Desarme");
}

void ESC::printDebug() const
{
    Serial.printf("[ESC]   Throttle: %5.2f\n", getThrottle());
}