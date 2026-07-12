#include "task_sonar.h"

// Mutex
extern SemaphoreHandle_t dataMutex;

// ─── Variables internes à cette tâche (static : invisibles ailleurs) ────────
static float distance_filt[NB_CANAUX] = {SONAR_DISTANCE_REF_AVANT, SONAR_DISTANCE_REF_ARRIERE_GAUCHE, SONAR_DISTANCE_REF_ARRIERE_DROIT};
static int PINS_SONARS[NB_CANAUX] = {Analog_Sonar_Avant, Analog_Sonar_Arriere_Gauche, Analog_Sonar_Arriere_Gauche};

// ─── Variables partagées (extern dans task_sonar.h) ─────────────────────────
float Sonar_distance[NB_CANAUX] = {SONAR_INIT_DISTANCE, SONAR_INIT_DISTANCE, SONAR_INIT_DISTANCE};
float Sonar_temps_us            =  SONAR_INIT_TIME;

// Fonction lecture de sonar
float lire_sonar(int pin, float &dist_filt)
{
  int raw = analogRead(pin);
  float voltage = (raw / SONAR_ADC_BITS) * SONAR_VREF;
  float courant_mA = (voltage / SONAR_RESISTANCE) * 1000.0f;
  float newDist = (courant_mA - SONAR_I_MIN_MA) / (SONAR_I_MAX_MA - SONAR_I_MIN_MA)
                  * (SONAR_DIST_MAX_CM - SONAR_DIST_MIN_CM) + SONAR_DIST_MIN_CM;

  dist_filt = SONAR_ALPHA * newDist + (1.0f - SONAR_ALPHA) * dist_filt;

  if (dist_filt < SONAR_DIST_MIN_CM) dist_filt = SONAR_DIST_MIN_CM;
  if (dist_filt > SONAR_DIST_MAX_CM) dist_filt = SONAR_DIST_MAX_CM;

  return dist_filt;
}

// Tache de lecture des sonars
void Task_LectureSonar(void *ptr)
{
  (void) ptr;
  TickType_t lastWakeTime = xTaskGetTickCount();

  while (1)
  {
    uint32_t t_debut = micros();

    float dist[NB_CANAUX];

    for(int i=0; i<NB_CANAUX; i++){
      dist[i] = lire_sonar(PINS_SONARS[i], distance_filt[i]);
      distance_filt[i] = dist[i];
    }

    float duree_us = (float)(micros() - t_debut);

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      for(int i=0; i<NB_CANAUX; i++){
        Sonar_distance[i] = dist[i];
      }
      Sonar_temps_us = duree_us;
      xSemaphoreGive(dataMutex);
    }

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_SONAR_MS));
  }
}