// =====================================================
//  task_propulsion.cpp
//  Tâche FreeRTOS — propulsion, 50 Hz.
//  Rampe et envoi repris de Real_V1 (fonction StateMachine(target)).
// =====================================================

#include "task_propulsion.h"
#include "task_esc_data.h"
#include "task_watchdog.h"

// ─── Définition des variables partagées ───
int   Prop_target   = 0;
float Prop_ramped   = 0.0f;
float Prop_temps_us = 0.0f;

// ─── État privé ───
static float rampedTarget = 0.0f;

static inline int iclamp(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

// Real_V1 : StateMachine(target) — appelée toutes les 20 ms
static void AppliquerConsigne(int target)
{
    if (target == 0) {
        // Arret : coupure nette. En mode vitesse, une consigne sous le
        // Minimum ERPM du VESC laisse le moteur en roue libre (pas de frein).
        rampedTarget = 0;
    } else {
        bool memeSens = (rampedTarget > 0) == (target > 0);

        // Depart a l'arret, ou inversion une fois redescendu au minimum :
        // on saute directement a ERPM_MIN_UTILE au lieu de traverser
        // la zone 0..1800 ou le moteur tourne mal.
        if (rampedTarget == 0 ||
            (!memeSens && fabsf(rampedTarget) <= ERPM_MIN_UTILE)) {
            rampedTarget = (target > 0) ? ERPM_MIN_UTILE : -ERPM_MIN_UTILE;
        }

        // Rampe normale au-dessus du minimum (et descente progressive
        // avant une inversion de sens, pour menager les helices).
        if (rampedTarget < target) {
            rampedTarget = fminf(rampedTarget + RAMP_STEP, (float)target);
        } else if (rampedTarget > target) {
            rampedTarget = fmaxf(rampedTarget - RAMP_STEP, (float)target);
        }
    }

    ESC_EnvoyerERPM((int32_t)(VESC_A_INVERSE ? -rampedTarget : rampedTarget),
                    (int32_t)(VESC_B_INVERSE ? -rampedTarget : rampedTarget));
}

// =====================================================
//  Task_Propulsion — 50 Hz
// =====================================================
void Task_Propulsion(void *ptr)
{
    (void) ptr;
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (1)
    {
        uint32_t t_debut = micros();

        // 1) Dernière commande de la machine d'état
        int    target = 0;
        FsmCmd cmd;
        if (xQueuePeek(qCmd, &cmd, 0) == pdTRUE) {
            if ((xTaskGetTickCount() - cmd.tick) <= pdMS_TO_TICKS(PROP_CMD_TIMEOUT_MS)) {
                target = cmd.target;
            }
            // sinon : machine d'état figée → consigne 0
        }

        // 2) Sécurités du watchdog
        EventBits_t bits = xEventGroupGetBits(egEtat);
        if (bits & EVT_ERR_CRITIQUE) target = 0;
#if LIMITE_TEMP_ACTIVE
        if (bits & EVT_SURCHAUFFE) {
            target = iclamp(target, -ERPM_MAX_SURCHAUFFE, ERPM_MAX_SURCHAUFFE);
        }
#endif

        // 3) Rampe + envoi aux VESC
        AppliquerConsigne(target);

        float duree_us = (float)(micros() - t_debut);

        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            Prop_target   = target;
            Prop_ramped   = rampedTarget;
            Prop_temps_us = duree_us;
            xSemaphoreGive(dataMutex);
        }

        wd_vivant(T_PROP);
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_PROP_MS));
    }
}

static_assert(ERPM_MAX_SURCHAUFFE >= ERPM_MIN_UTILE,
              "le plafond de surchauffe doit rester dans la plage utile");
