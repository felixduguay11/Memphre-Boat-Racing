// =====================================================
//  task_state_machine.cpp
//  Tâche FreeRTOS — machine d'état, 50 Hz.
//
//  Portage du miniature (loop) → tâche FreeRTOS, avec séparation
//  Propulsion / StateMachine demandée :
//    - lit le snapshot manette publié par Task_Propulsion (Prop_*)
//    - met à jour la machine d'état
//    - publie l'état résultant (SM_*) pour Task_Propulsion,
//      Task_foils_Control et Task_RPi.
// =====================================================

#include "task_state_machine.h"
#include "task_propulsion.h"   // snapshot manette Prop_rc_*
#include "StateMachine.h"

extern SemaphoreHandle_t dataMutex;

// ─── Objet possédé EN EXCLUSIVITÉ par cette tâche ───
static StateMachine sm;

// ─── Définition des variables partagées (déclarées extern dans le .h) ───
TopState SM_top_state           = TopState::IDLE;
RunState SM_run_state           = RunState::NEUTRE;
bool     SM_foil_control_active = false;
float    SM_control_time_us     = 0.0f;

// =====================================================
//  Task_StateMachine — 50 Hz
// =====================================================
void Task_StateMachine(void *ptr)
{
    (void) ptr;

    sm.begin();

    TickType_t lastWakeTime = xTaskGetTickCount();

    while (1)
    {
        uint32_t t_debut = micros();

        // 1) Lire le snapshot manette publié par Task_Propulsion.
        //    Valeurs sûres par défaut (failsafe) si le mutex échoue.
        SMInputs in;
        in.throttle = 0.0f;
        in.rudder   = 0.0f;
        in.switchA  = false;
        in.switchB  = false;   // inutilisé
        in.switchC  = false;
        in.rcValid  = false;

        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            in.throttle = Prop_rc_throttle;
            in.rudder   = Prop_rc_rudder;
            in.switchA  = Prop_rc_switchA;
            in.switchC  = Prop_rc_switchC;
            in.rcValid  = Prop_rc_valid;
            xSemaphoreGive(dataMutex);
        }

        // 2) Mise à jour de la machine d'état.
        sm.update(in);

        TopState top = sm.getTopState();
        RunState run = sm.getRunState();
        bool foils_actifs = (top == TopState::RUN && run == RunState::CONTROLE);

        float duree_us = (float)(micros() - t_debut);

        // 3) Publier l'état (lu par Propulsion + Foils + RPi).
        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            SM_top_state           = top;
            SM_run_state           = run;
            SM_foil_control_active = foils_actifs;
            SM_control_time_us     = duree_us;
            xSemaphoreGive(dataMutex);
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_SM_MS));
    }
}
