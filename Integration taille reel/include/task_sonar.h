#pragma once

#include <arduino_freertos.h>
#include <semphr.h>

// =====================================================
// Paramètres sonar — IFM UGT207 sur 150Ω, ADC 12 bits
//   4mA  × 150Ω = 0.60V → 20cm
//   20mA × 150Ω = 3.00V → 200cm
//   ADC 12 bits (0-4095), Vref = 3.3V
// =====================================================
const float VREF        = 3.3f;
const float ADC_BITS    = 4095.0f;
const float R_SHUNT     = 150.0f;
const float I_MIN_MA    = 4.0f;
const float I_MAX_MA    = 20.0f;
const float DIST_MIN_CM = 20.0f;
const float DIST_MAX_CM = 200.0f;

const float ALPHA_SONAR      = 0.2f;
const int   PIN_SONAR        = A0;
const int   PERIODE_SONAR_MS = 20;  // 50Hz

// =====================================================
// Variables partagées — écrites par Task_LectureSonar
// Lues par Task_Controle et Task_Debug via dataMutex
// =====================================================
extern float g_distance;
extern float g_temps_sonar_us;

// =====================================================
// Fonction utilitaire — accessible depuis Task_Controle
// Chaque appelant passe son propre filtre pour rester
// indépendant
// =====================================================
float lire_sonar(int pin, float &filt);

// =====================================================
// Tâche FreeRTOS
// =====================================================
void Task_LectureSonar(void *ptr);
