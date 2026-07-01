#pragma once

// =====================================================
//  task_state_machine.h
//  Tâche FreeRTOS : machine d'état + pilotage des 2 ESC
//  et du servo de direction à partir de la manette RC.
//
//  Portage du code miniature (loop) vers l'architecture
//  FreeRTOS du projet hydrofoil.
//
//  Cette tâche possède en exclusivité :
//    - le récepteur RC   (RCReceiver)
//    - les 2 ESC          (ESC)
//    - le servo direction (inline, PWMServo)
//    - la machine d'état  (StateMachine)
//
//  Les foils NE sont PAS pilotés ici : Task_foils_Control
//  lit le flag SM_foil_control_active pour s'activer
//  (un seul écrivain par servo → pas de course critique).
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"
#include "StateMachine.h"   // pour les enums TopState / RunState

// ─── Variables partagées (publiées par Task_StateMachine) ───
extern TopState SM_top_state;
extern RunState SM_run_state;
extern bool     SM_foil_control_active;   // true uniquement en RUN/CONTROLE
extern float    SM_rc_throttle;           // [0.0, +1.0]
extern float    SM_rc_rudder;             // [-1.0, +1.0]
extern bool     SM_rc_valid;
extern float    SM_control_time_us;       // durée d'exécution de la tâche

// ─── Tâche FreeRTOS ───
void Task_StateMachine(void *ptr);
