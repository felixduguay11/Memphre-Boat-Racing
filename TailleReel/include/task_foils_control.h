#pragma once

#include <arduino_freertos.h>
#include <semphr.h>
#include <PWMServo.h>
#include "config.h"

// Paramètres servo
const int SERVO_NEUTRAL[NB_CANAUX]  = {SERVO_AVANT_NEUTRAL, SERVO_ARRIERE_GAUCHE_NEUTRAL, SERVO_ARRIERE_DROIT_NEUTRAL};
const int SERVO_MIN[NB_CANAUX]      = {SERVO_AVANT_MIN, SERVO_ARRIERE_GAUCHE_MIN, SERVO_ARRIERE_DROIT_MIN};
const int SERVO_MAX[NB_CANAUX]      = {SERVO_AVANT_MAX, SERVO_ARRIERE_GAUCHE_MAX, SERVO_ARRIERE_DROIT_MAX};

// Paramètres PID Fuzzy
const float distance_ref[NB_CANAUX] = {H_DISTANCE_REF_AVANT, H_DISTANCE_REF_ARRIERE_GAUCHE, H_DISTANCE_REF_ARRIERE_DROIT};
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
extern float HPR_cmd_servos[NB_CANAUX]; 
extern float HPR_control_time_us;       

// Tâche FreeRTOS
void Task_foils_Control(void *ptr);