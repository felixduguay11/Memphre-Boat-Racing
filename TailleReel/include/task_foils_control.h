#pragma once

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"
#include "servos.h"     // sortie en µs, Min / Neutre / Max calibrés (EEPROM)

// Paramètres PID Fuzzy
// Consigne de distance de chaque sonar (cm) = zéro mesuré sur l'eau + H_VOL_x_CM
extern float distance_ref[NB_CANAUX];
extern float cmd_filt[NB_CANAUX];

// Hauteur — un par foil
extern float H_prev_dist[NB_CANAUX];
extern float H_integral[NB_CANAUX];
extern float H_deriv[NB_CANAUX];

// Pitch — un seul état
extern float P_prev_pitch;
extern float P_integral;
extern float P_deriv;

// Roll — un seul état
extern float R_prev_roll;
extern float R_integral;
extern float R_deriv;

// Variables partagées
extern float H_outputs[NB_CANAUX];
extern float P_output;                  
extern float R_output;                  
extern float HPR_cmd_servos[NB_CANAUX];   // µs envoyés (0 = servo pas commandé)
extern float HPR_control_time_us;       

// Tâche FreeRTOS
void Task_foils_Control(void *ptr);