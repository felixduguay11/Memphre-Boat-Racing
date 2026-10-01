#pragma once

// =====================================================
//  task_pilote.h
//  Tâche FreeRTOS : entrées du pilote (priorité 2, 50 Hz).
//
//  Lit le levier de vitesse et les switchs, applique le
//  mapping du levier (Real_V1), publie le snapshot dans
//  Shared Data et l'envoie à la machine d'état (qEvents).
//  Aucune décision d'état ici.
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"
#include "rtos_com.h"

// ─── Variables partagées (publiées par Task_Pilote) ───
extern PiloteData Pilote_data;
extern float      Pilote_temps_us;

// ─── Tâche FreeRTOS ───
void Task_Pilote(void *ptr);
