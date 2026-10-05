#pragma once

// =====================================================
//  task_propulsion.h
//  Tâche FreeRTOS : propulsion (priorité 3, 50 Hz).
//
//  Lit la commande de la machine d'état (qCmd, xQueuePeek),
//  applique les sécurités puis la rampe de Real_V1 et envoie
//  la consigne aux deux VESC (eRPM, duty ou courant : MODE_COMMANDE).
//
//  Sécurités (indépendantes de la machine d'état) :
//    - commande plus vieille que PROP_CMD_TIMEOUT_MS → 0
//    - EVT_ERR_CRITIQUE (watchdog)                  → 0
//    - EVT_SURCHAUFFE   (watchdog) → plafond ±CMD_MAX_SURCHAUFFE
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"
#include "rtos_com.h"

// ─── Variables partagées (publiées par Task_Propulsion) ───
extern int   Prop_target;     // consigne finale (après sécurités), avant rampe
extern float Prop_ramped;     // consigne rampée envoyée (avant inversion B)
extern float Prop_temps_us;

// ─── Tâche FreeRTOS ───
void Task_Propulsion(void *ptr);
