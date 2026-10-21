#pragma once

// =====================================================
//  task_state_machine.h
//  Tâche FreeRTOS : machine d'état (priorité 4).
//
//  Purement réactive : bloquée sur qEvents (snapshot pilote
//  à 50 Hz + événements d'erreur du watchdog).
//
//  Logique = Real_V1 :
//    IDLE : switch ON relâché → consigne 0, armement perdu
//    RUN  : armement (levier vu au neutre), puis
//           F/R actif → REVERSE, sinon FORWARD ;
//           consigne 0 → NEUTRAL
//  Ajouts :
//    ARRET    : erreur critique du watchdog → consigne 0,
//               armement perdu (retour levier au neutre exigé)
//    CONTROLE : sous-état de FORWARD, foils actifs (voir config.h)
//
//  Sorties : qCmd (mailbox) → Propulsion,
//            egEtat (EVT_RUN / EVT_CONTROLE / EVT_ARRET),
//            Shared Data (SM_*) → RPi.
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"
#include "rtos_com.h"

// ─── Variables partagées (publiées par Task_StateMachine) ───
extern ControlMode SM_mode;
extern RunMode     SM_run;
extern int         SM_target;      // consigne (unité de MODE_COMMANDE) avant rampe / plafond
extern bool        SM_arme;        // levier armé
extern float       SM_temps_us;

// ─── Tâche FreeRTOS ───
void Task_StateMachine(void *ptr);
