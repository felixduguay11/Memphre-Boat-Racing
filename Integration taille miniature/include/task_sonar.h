#pragma once

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"

// ─── Variables internes à Task_LectureSonar ────────────────────────────────
extern float Sonar_distance[NB_CANAUX];
extern float Sonar_temps_us;

// ─── Fonction utilitaire ───────────────────────────────────────────────────
float lire_sonar(int pin, float &filt);

// ─── Tâche FreeRTOS ───────────────────────────────────────────────────────
void Task_LectureSonar(void *ptr);