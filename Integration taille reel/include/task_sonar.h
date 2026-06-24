#pragma once

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"

// Variables sonars
extern float distance_filt[NB_CANAUX];
extern int PINS_SONARS[NB_CANAUX];

// Variables partagées
extern float Sonar_distance[NB_CANAUX];
extern float Sonar_temps_us;

// Fonction
float lire_sonar(int pin, float &filt);

// Tâche FreeRTOS
void Task_LectureSonar(void *ptr);
