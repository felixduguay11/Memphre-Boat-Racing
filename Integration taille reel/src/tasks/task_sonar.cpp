#include "task_sonar.h"

// =====================================================
// Définition des variables partagées
// déclarées extern dans task_sonar.h
// =====================================================
float g_distance       = 0.0f;
float g_temps_sonar_us = 0.0f;

// Filtre interne — propre à cette tâche, non partagé
// Task_Controle a le sien dans main.cpp
static float s_distance_filt = 70.0f;

// Mutex défini dans main.cpp
extern SemaphoreHandle_t dataMutex;


// =====================================================
// lire_sonar()
//
// Étapes :
//   1. Lecture brute ADC (12 bits)
//   2. ADC → tension (V)
//   3. Tension → courant (mA) via loi d'Ohm : I = V / R
//   4. Courant → distance (cm) par interpolation linéaire
//   5. Filtre passe-bas exponentiel
//   6. Contrainte physique [DIST_MIN_CM, DIST_MAX_CM]
//
// filt : référence vers le filtre de l'appelant
// =====================================================
float lire_sonar(int pin, float &filt)
{
  // Étape 1 — Lecture brute ADC
  int raw = analogRead(pin);

  // Étape 2 — ADC → tension (V)
  float voltage = (raw / ADC_BITS) * VREF;

  // Étape 3 — Tension → courant (mA)
  float courant_mA = (voltage / R_SHUNT) * 1000.0f;

  // Étape 4 — Courant → distance (cm)
  //   4mA  = DIST_MIN_CM (20cm)
  //   20mA = DIST_MAX_CM (200cm)
  float newDist = (courant_mA - I_MIN_MA) / (I_MAX_MA - I_MIN_MA)
                  * (DIST_MAX_CM - DIST_MIN_CM) + DIST_MIN_CM;

  // Étape 5 — Filtre passe-bas exponentiel
  filt = ALPHA_SONAR * newDist + (1.0f - ALPHA_SONAR) * filt;

  // Étape 6 — Contrainte physique du capteur
  if (filt < DIST_MIN_CM) filt = DIST_MIN_CM;
  if (filt > DIST_MAX_CM) filt = DIST_MAX_CM;

  return filt;
}


// =====================================================
// Task_LectureSonar — priorité 2, période 20ms
//
// Rôle : lire le sonar et mettre à jour g_distance
//        pour la tâche debug (affichage uniquement)
//
// Note : Task_Controle lit aussi le sonar via lire_sonar()
//        avec son propre filtre — cette tâche est pour debug
// =====================================================
void Task_LectureSonar(void *ptr)
{
  (void) ptr;
  TickType_t lastWakeTime = xTaskGetTickCount();

  while (1)
  {
    uint32_t t_debut = micros();

    float dist = lire_sonar(PIN_SONAR, s_distance_filt);

    float duree_us = (float)(micros() - t_debut);

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      g_distance       = dist;
      g_temps_sonar_us = duree_us;
      xSemaphoreGive(dataMutex);
    }

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_SONAR_MS));
  }
}
