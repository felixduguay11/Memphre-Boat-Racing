#pragma once

// =====================================================
//  task_propulsion.h
//  Tâche FreeRTOS : PROPULSION du miniature.
//
//  Cette tâche possède EN EXCLUSIVITÉ :
//    - le récepteur FlySky   (RCReceiver)
//    - les 2 ESC             (ESC)
//    - le servo de direction (rudder, PWMServo inline)
//
//  Rôle chaque tick (50 Hz) :
//    1. lit la manette FlySky (rc.update)
//    2. publie le snapshot manette (Prop_*) pour Task_StateMachine
//    3. lit la décision d'état (SM_top_state / SM_run_state)
//    4. applique : ESC + rudder selon l'état
//       + FAILSAFE matériel : perte manette => arrêt immédiat
//
//  Les foils NE sont PAS pilotés ici (ils appartiennent à
//  Task_foils_Control) → règle « un seul écrivain par servo ».
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"
#include "task_state_machine.h"   // enums + SM_top_state / SM_run_state

// ─── Snapshot manette publié par Task_Propulsion ───
extern float Prop_rc_throttle;     // [0.0, +1.0]
extern float Prop_rc_rudder;       // [-1.0, +1.0]
extern bool  Prop_rc_switchA;      // armement RUN
extern bool  Prop_rc_switchC;      // marche arrière
extern bool  Prop_rc_valid;        // false = signal perdu
extern float Prop_control_time_us; // durée d'exécution de la tâche [µs]

// ─── Tâche FreeRTOS ───
void Task_Propulsion(void *ptr);
