#pragma once

#include <arduino_freertos.h>
#include <semphr.h>
#include <PWMServo.h>
#include "config.h"

// Paramètres servo
int SERVO_NEUTRAL[NB_CANAUX]    = {SERVO_AVANT_NEUTRAL, SERVO_ARRIERE_GAUCHE_NEUTRAL, SERVO_ARRIERE_DROIT_NEUTRAL};
int SERVO_MIN[NB_CANAUX]        = {SERVO_AVANT_MIN, SERVO_ARRIERE_GAUCHE_MIN, SERVO_ARRIERE_DROIT_MIN};
int SERVO_MAX[NB_CANAUX]        = {SERVO_AVANT_MAX, SERVO_ARRIERE_GAUCHE_MAX, SERVO_ARRIERE_DROIT_MAX};

// Paramètres PID Fuzzy
float prev_target[NB_CANAUX]    = {H_DISTANCE_REF_AVANT, H_DISTANCE_REF_ARRIERE_GAUCHE, H_DISTANCE_REF_ARRIERE_DROIT};
float distance_ref[NB_CANAUX]   = {H_DISTANCE_REF_AVANT, H_DISTANCE_REF_ARRIERE_GAUCHE, H_DISTANCE_REF_ARRIERE_DROIT};
float integral_pid[NB_CANAUX]   = {H_INIT_INTEGRAL, H_INIT_INTEGRAL, H_INIT_INTEGRAL};
float deriv_filt_pid[NB_CANAUX] = {H_INIT_DERIVE, H_INIT_DERIVE, H_INIT_DERIVE};
float cmd_filt[NB_CANAUX]       = {SERVO_AVANT_NEUTRAL, SERVO_ARRIERE_GAUCHE_NEUTRAL, SERVO_ARRIERE_DROIT_NEUTRAL};

// Variables partagées
extern float H_outputs[NB_CANAUX]       = {H_INIT_OUTPUTS, H_INIT_OUTPUTS, H_INIT_OUTPUTS};
extern float H_cmd_servos[NB_CANAUX]    = {H_INIT_CMD, H_INIT_CMD, H_INIT_CMD};
extern float H_control_time_us          =  H_INIT_TIME;

// Tâche FreeRTOS
void Task_Height_Control(void *ptr);
