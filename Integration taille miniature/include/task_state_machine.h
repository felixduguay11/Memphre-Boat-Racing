#pragma once

// =====================================================
//  task_state_machine.h
//  Tâche FreeRTOS : machine d'état (IDLE/RUN/STOP + sous-états).
//
//  Cette tâche possède EN EXCLUSIVITÉ l'objet StateMachine.
//  Elle NE lit PAS la manette directement et NE pilote AUCUN
//  actionneur : c'est Task_Propulsion qui lit la manette (FlySky)
//  et qui applique les décisions (ESC + rudder).
//
//  Flux de données (tout sous dataMutex, 1 seul écrivain / variable) :
//    Task_Propulsion  --(snapshot manette Prop_*)-->  Task_StateMachine
//    Task_StateMachine --(état SM_*)-->  Task_Propulsion + Task_foils_Control + RPi
//
//  Task_foils_Control peut lire SM_foil_control_active pour n'activer
//  son PID qu'en RUN/CONTROLE (gating optionnel — voir mon message).
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"
#include "StateMachine.h"   // enums TopState / RunState + SMInputs

// ─── Variables partagées publiées par Task_StateMachine ───
extern TopState SM_top_state;
extern RunState SM_run_state;
extern bool     SM_foil_control_active;   // true uniquement en RUN/CONTROLE
extern float    SM_control_time_us;       // durée d'exécution de la tâche [µs]

// ─── Tâche FreeRTOS ───
void Task_StateMachine(void *ptr);
