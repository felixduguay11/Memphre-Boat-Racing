// =====================================================
//  task_state_machine.cpp
//  Tâche FreeRTOS — machine d'état (Real_V1 + ARRET + CONTROLE)
// =====================================================

#include "task_state_machine.h"
#include "task_xsens.h"
#include "task_sonar.h"
#include "task_watchdog.h"

// ─── Définition des variables partagées ───
ControlMode SM_mode     = ControlMode::IDLE;
RunMode     SM_run      = RunMode::NEUTRAL;
int         SM_target   = 0;
bool        SM_arme     = false;
float       SM_temps_us = 0.0f;

// ─── État privé ───
static bool levierArme = false;

// Snapshot « sûr » : switch ON relâché, levier au repos
static const PiloteData PILOTE_SUR = {false, false, false, false, 0, 0, 0, 0};

// FORWARD → CONTROLE (entrée) / CONTROLE → FORWARD (sortie, hystérésis)
static bool ControleAutorise(bool deja_en_controle, const PiloteData &p,
                             bool v_ok, float v_kmh, bool sonars_ok,
                             EventBits_t bits)
{
#if FOILS_ACTIFS
    if (!p.switch_ctl)              return false;   // switch Relay_Control
    if (!sonars_ok)                 return false;   // I/O des sonars
    if (!v_ok)                      return false;   // vitesse GPS valide
    if (bits & EVT_ERR_CAPTEUR)     return false;   // Xsens/Sonar/Foils en défaut
    float seuil = deja_en_controle ? (SM_VITESSE_CONTROLE_KMH - SM_VITESSE_HYST_KMH)
                                   :  SM_VITESSE_CONTROLE_KMH;
    return v_kmh >= seuil;
#else
    (void) deja_en_controle; (void) p; (void) v_ok; (void) v_kmh;
    (void) sonars_ok; (void) bits;
    return false;                                   // pas de foils : jamais CONTROLE
#endif
}

// =====================================================
//  Task_StateMachine — réveillée par qEvents
// =====================================================
void Task_StateMachine(void *ptr)
{
    (void) ptr;

    PiloteData  pil            = PILOTE_SUR;
    ControlMode currentMode    = ControlMode::IDLE;
    RunMode     currentRunMode = RunMode::NEUTRAL;

    while (1)
    {
        // 1) Attente d'un événement (snapshot pilote ou erreur)
        FsmEvent ev;
        if (xQueueReceive(qEvents, &ev, pdMS_TO_TICKS(FSM_TIMEOUT_MS)) == pdTRUE) {
            if (ev.type == EvType::PILOTE) pil = ev.pilote;
            // EvType::ERREUR : on réévalue avec le dernier snapshot
        } else {
            pil = PILOTE_SUR;   // plus de nouvelles du pilote : état sûr
        }

        uint32_t t_debut = micros();

        // 2) Entrées de CONTROLE (Shared Data) + bits du watchdog
        bool  v_ok      = false;
        float v_kmh     = 0.0f;
        bool  sonars_ok = true;
        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            v_ok  = Xsens_data.vel_valid;
            v_kmh = Xsens_data.speed * 3.6f;          // m/s → km/h
            for (int i = 0; i < NB_CANAUX; i++) {
                sonars_ok = sonars_ok && Sonar_io[i];
            }
            xSemaphoreGive(dataMutex);
        }
        EventBits_t bits = xEventGroupGetBits(egEtat);

        // 3) Logique d'état
        RunMode runPrec = currentRunMode;
        int     target  = 0;

        if (bits & EVT_ERR_CRITIQUE) {
            // AJOUT : erreur critique → ARRET, il faudra réarmer
            currentMode    = ControlMode::ARRET;
            currentRunMode = RunMode::NEUTRAL;
            target         = 0;
            levierArme     = false;
        }
        //  ── Real_V1 ──────────────────────────────────────────
        //  RUN exige un LOW franc sur I_Switch_ON (switch fermé).
        //  Tout le reste (fil débranché, switch ouvert) = IDLE.
        else if (!pil.switch_on) {
            currentMode    = ControlMode::IDLE;
            currentRunMode = RunMode::NEUTRAL;
            target         = 0;
            levierArme     = false;      // il faudra repasser par le neutre
        }
        else {
            currentMode = ControlMode::RUN;

            // Armement : on refuse toute consigne tant que le levier
            // n'a pas été vu au neutre au moins une fois.
            if (ARMEMENT_REQUIS && !levierArme) {
                if (pil.levier_raw <= LEVIER_RAW_ARME) levierArme = true;
                currentRunMode = RunMode::NEUTRAL;
                target         = 0;
            }
            else if (pil.switch_fr) {
                currentRunMode = RunMode::REVERSE;
                target         = pil.target_rev;
            }
            else {
                currentRunMode = RunMode::FORWARD;
                target         = pil.target_fwd;
            }

            // Levier au repos : on affiche NEUTRAL plutôt que FORWARD à 0.
            if (target == 0) currentRunMode = RunMode::NEUTRAL;
            //  ─────────────────────────────────────────────────

            // AJOUT : CONTROLE = FORWARD + foils actifs
            if (currentRunMode == RunMode::FORWARD &&
                ControleAutorise(runPrec == RunMode::CONTROLE, pil, v_ok, v_kmh, sonars_ok, bits)) {
                currentRunMode = RunMode::CONTROLE;
            }
        }

        // 4) Publication
        FsmCmd cmd;
        cmd.mode   = currentMode;
        cmd.run    = currentRunMode;
        cmd.target = target;
        cmd.tick   = xTaskGetTickCount();
        xQueueOverwrite(qCmd, &cmd);

        EventBits_t actifs = 0;
        if (currentMode    == ControlMode::RUN)   actifs |= EVT_RUN;
        if (currentMode    == ControlMode::ARRET) actifs |= EVT_ARRET;
        if (currentRunMode == RunMode::CONTROLE)  actifs |= EVT_CONTROLE;
        xEventGroupClearBits(egEtat, EVT_FSM_MASK & ~actifs);
        if (actifs) xEventGroupSetBits(egEtat, actifs);

        float duree_us = (float)(micros() - t_debut);

        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            SM_mode     = currentMode;
            SM_run      = currentRunMode;
            SM_target   = target;
            SM_arme     = levierArme;
            SM_temps_us = duree_us;
            xSemaphoreGive(dataMutex);
        }

        wd_vivant(T_FSM);
    }
}
