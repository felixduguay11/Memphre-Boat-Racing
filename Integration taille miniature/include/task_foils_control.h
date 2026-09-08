#pragma once

#include <arduino_freertos.h>
#include <semphr.h>
#include <PWMServo.h>
#include "config.h"

// ─── Paramètres servo (const : linkage interne, safe dans un header) ───
const int SERVO_NEUTRAL[NB_CANAUX]  = {SERVO_AVANT_NEUTRAL, SERVO_ARRIERE_GAUCHE_NEUTRAL, SERVO_ARRIERE_DROIT_NEUTRAL};
const int SERVO_MIN[NB_CANAUX]      = {SERVO_AVANT_MIN, SERVO_ARRIERE_GAUCHE_MIN, SERVO_ARRIERE_DROIT_MIN};
const int SERVO_MAX[NB_CANAUX]      = {SERVO_AVANT_MAX, SERVO_ARRIERE_GAUCHE_MAX, SERVO_ARRIERE_DROIT_MAX};

const float distance_ref[NB_CANAUX] = {H_DISTANCE_REF_AVANT, H_DISTANCE_REF_ARRIERE_GAUCHE, H_DISTANCE_REF_ARRIERE_DROIT};

// ─── Variables partagées (écrites par Task_foils_Control, lues par Task_RPi) ─
extern float H_outputs[NB_CANAUX];
extern float P_output;
extern float R_output;
extern float HPR_cmd_servos[NB_CANAUX];
extern float HPR_control_time_us;
extern float R_ref_output;

// ─── Tâche FreeRTOS ───────────────────────────────────────────────────
void Task_foils_Control(void *ptr);