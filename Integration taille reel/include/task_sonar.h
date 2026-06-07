#pragma once

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"

// Variables sonars
float distance_filt[NB_CANAUX] = {SONAR_DISTANCE_REF_AVANT, SONAR_DISTANCE_REF_ARRIERE_GAUCHE, SONAR_DISTANCE_REF_ARRIERE_DROIT};
int PINS_SONARS[NB_CANAUX] = {Analog_Sonar_Avant, Analog_Sonar_Arriere_Gauche, Analog_Sonar_Arriere_Droit};

// Variables partagées
extern float Sonar_distance[NB_CANAUX] = {SONAR_INIT_DISTANCE, SONAR_INIT_DISTANCE, SONAR_INIT_DISTANCE};
extern float Sonar_temps_us            =  SONAR_INIT_TIME;

// Fonction
float lire_sonar(int pin, float &filt);

// Tâche FreeRTOS
void Task_LectureSonar(void *ptr);
