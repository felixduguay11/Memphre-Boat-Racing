// =====================================================
//  task_state_machine.cpp
//  Tâche FreeRTOS — machine d'état (Real_V1 + ARRET + CONTROLE + CRUISE)
// =====================================================

#include "task_state_machine.h"
#include <cmath>
#include "task_xsens.h"
#include "task_sonar.h"
#include "task_esc_data.h"
#include "task_watchdog.h"
#include "servos.h"

// ─── Définition des variables partagées ───
ControlMode SM_mode     = ControlMode::IDLE;
RunMode     SM_run      = RunMode::NEUTRAL;
int         SM_target   = 0;
bool        SM_arme     = false;
float       SM_temps_us = 0.0f;
uint8_t     SM_cruise     = CRUISE_OFF;
uint32_t    SM_cruise_ms  = 0;
uint8_t     SM_cruise_fin = CRUISE_FIN_AUCUNE;
bool        SM_foils_arme  = false;
bool        SM_controle    = false;
uint32_t    SM_controle_ms = 0;
uint8_t     SM_foils_fin   = FOILS_FIN_AUCUNE;
volatile uint32_t Cruise_vie_ms = 0;

// ─── État privé ───
static bool levierArme = false;

// ─── Bouton Foils de l'UI (état privé) ───
static bool     foilsArme     = false;     // pop-up Foils confirmé
static uint8_t  foilsFin      = FOILS_FIN_AUCUNE;
static uint32_t controleDebutMs = 0;
static uint32_t controleDureeMs = 0;       // durée en CONTROLE (figée à la sortie)

static void FoilsDesarmer(uint8_t raison)
{
    if (!foilsArme) return;
    foilsArme = false;
    foilsFin  = raison;
}

// ─── Cruise control (état privé) ───
static uint8_t  cruiseMode     = CRUISE_OFF;
static bool     cruiseRearme   = false;    // après un cruise : levier au neutre exigé
static uint32_t cruiseDebutMs  = 0;
static uint32_t cruiseDureeMs  = 0;        // durée de l'activation (figée à la sortie)
static uint8_t  cruiseFin      = CRUISE_FIN_AUCUNE;

// Toute sortie du cruise : durée figée, raison notée, et le levier
// devra repasser au neutre avant de commander de nouveau.
static void CruiseSortir(uint8_t raison)
{
    if (cruiseMode == CRUISE_OFF) return;
    cruiseDureeMs = millis() - cruiseDebutMs;
    cruiseMode    = CRUISE_OFF;
    cruiseFin     = raison;
    cruiseRearme  = true;
    levierArme    = false;
}

#if CRUISE_ACTIF
static float    cruiseConsigne = 0.0f;     // consigne du cruise (rampe ERPM / boucle vitesse)
static uint32_t cruiseBoucleMs = 0;        // dernier passage du cruise

static_assert(CRUISE_ERPM >= CMD_MIN_UTILE && CRUISE_ERPM <= CMD_MAX_FORWARD,
              "CRUISE_ERPM hors de la plage de consigne du levier");
static_assert(CRUISE_RAMPE_PAS > 0, "CRUISE_RAMPE_PAS doit etre positif");

// Cruise ERPM : la consigne rejoint CRUISE_ERPM de CRUISE_RAMPE_PAS
// eRPM toutes les 20 ms (au prorata si un passage arrive en retard)
static float CruiseRampe(float consigne, float dt_s)
{
    float pas = (float)CRUISE_RAMPE_PAS * (dt_s / 0.020f);
    if (consigne < CRUISE_ERPM) consigne = fminf(consigne + pas, (float)CRUISE_ERPM);
    else                        consigne = fmaxf(consigne - pas, (float)CRUISE_ERPM);
    return consigne;
}
static_assert(CRUISE_VIT_ERPM_MAX >= CMD_MIN_UTILE && CRUISE_VIT_ERPM_MAX <= CMD_MAX_FORWARD,
              "CRUISE_VIT_ERPM_MAX hors de la plage de consigne du levier");

// Cruise vitesse : boucle intégrale à deadband, appelée à chaque
// passage de la machine d'état (50 Hz). Retourne la nouvelle consigne.
//   v_kmh     : vitesse GPS
//   erpm_reel : régime réel moyen des moteurs (|eRPM|)
static float CruiseVitesse(float consigne, float v_kmh, float erpm_reel, float dt_s)
{
    float err = CRUISE_VITESSE_KMH - v_kmh;
    if (fabsf(err) > CRUISE_VIT_DEADBAND_KMH) {
        float pente = CRUISE_VIT_GAIN * err;                      // eRPM/s
        if (pente >  CRUISE_VIT_PENTE_MAX) pente =  CRUISE_VIT_PENTE_MAX;
        if (pente < -CRUISE_VIT_PENTE_MAX) pente = -CRUISE_VIT_PENTE_MAX;
        // Moteurs au plafond de courant : la consigne ne monte plus
        if (pente > 0.0f && (consigne - erpm_reel) > CRUISE_VIT_ECART_MAX) pente = 0.0f;
        consigne += pente * dt_s;
    }
    if (consigne < CMD_MIN_UTILE)       consigne = CMD_MIN_UTILE;
    if (consigne > CRUISE_VIT_ERPM_MAX) consigne = CRUISE_VIT_ERPM_MAX;
    return consigne;
}
#endif

// Snapshot « sûr » : switch ON relâché, levier au repos
static const PiloteData PILOTE_SUR = {};   // tout à false / 0

// FORWARD → CONTROLE (entrée) / CONTROLE → FORWARD (sortie, hystérésis)
// (Le switch Relay_Control n'est plus une condition : bouton Foils de l'UI.)
static bool ControleAutorise(bool deja_en_controle, bool arme,
                             bool v_ok, float v_kmh, bool sonars_ok,
                             EventBits_t bits)
{
#if FOILS_ACTIFS
    if (!arme)                      return false;   // bouton Foils de l'UI
    if (FOILS_HAUTEUR_ACTIF && !sonars_ok) return false;   // I/O des sonars + zéro
    if (bits & EVT_ERR_CAPTEUR)     return false;   // Xsens/Sonar/Foils en défaut
    if (!Servos_TousValides())      return false;   // servos pas calibrés (UI → Calibration)
#if FOILS_CONDITION_VITESSE
    if (!v_ok)                      return false;   // vitesse GPS valide
    float seuil = deja_en_controle ? (SM_VITESSE_CONTROLE_KMH - SM_VITESSE_HYST_KMH)
                                   :  SM_VITESSE_CONTROLE_KMH;
    return v_kmh >= seuil;
#else
    (void) deja_en_controle; (void) v_ok; (void) v_kmh;
    return true;                                    // le bouton suffit
#endif
#else
    (void) deja_en_controle; (void) arme; (void) v_ok; (void) v_kmh;
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
    bool        controlePrec   = false;     // foils actifs au cycle précédent (hystérésis)

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

        // 2) Entrées de CONTROLE et de CRUISE (Shared Data) + bits du watchdog
        bool     v_ok      = false;
        float    v_kmh     = 0.0f;
        uint32_t v_t_ms    = 0;
        bool     sonars_ok = true;
        float    erpm_reel = 0.0f;                    // moyenne des |eRPM| des VESC vivants
        int      nb_vesc   = 0;
        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            v_ok   = Xsens_data.vel_valid;
            v_kmh  = Xsens_data.speed * 3.6f;         // m/s → km/h
            v_t_ms = Xsens_data.t_ms;
            for (int i = 0; i < NB_CANAUX; i++) {
                sonars_ok = sonars_ok && Sonar_io[i];
            }
            sonars_ok = sonars_ok && Sonar_zero_ok;   // zéro sur l'eau fait (UI → Calibration)
            for (int k = 0; k < NB_VESC; k++) {
                if (ESC_data[k].vivant) { erpm_reel += fabsf((float)ESC_data[k].erpm); nb_vesc++; }
            }
            xSemaphoreGive(dataMutex);
        }
        if (nb_vesc > 0) erpm_reel /= (float)nb_vesc;
        EventBits_t bits = xEventGroupGetBits(egEtat);
        // Demandes de l'UI (boutons cruise) : lues et effacées à chaque
        // passage — une demande refusée n'est jamais gardée en attente.
        EventBits_t dem  = xEventGroupClearBits(egEtat, EVT_CRUISE_MASK);
        // Bouton Foils de l'UI : idem (une demande refusée n'est pas gardée)
        EventBits_t demF = xEventGroupClearBits(egEtat, EVT_FOILS_MASK);

        // 3) Logique d'état
        int     target   = 0;
        bool    controle = false;                     // foils actifs (EVT_CONTROLE)

        if (bits & EVT_ERR_CRITIQUE) {
            // AJOUT : erreur critique → ARRET, il faudra réarmer
            currentMode    = ControlMode::ARRET;
            currentRunMode = RunMode::NEUTRAL;
            target         = 0;
            levierArme     = false;
            CruiseSortir(CRUISE_FIN_ARRET);
            FoilsDesarmer(FOILS_FIN_ARRET);
        }
        //  ── Real_V1 ──────────────────────────────────────────
        //  RUN exige un LOW franc sur I_Switch_ON (switch fermé).
        //  Tout le reste (fil débranché, switch ouvert) = IDLE.
        else if (!pil.switch_on) {
            currentMode    = ControlMode::IDLE;
            currentRunMode = RunMode::NEUTRAL;
            target         = 0;
            levierArme     = false;      // il faudra repasser par le neutre
            CruiseSortir(CRUISE_FIN_IDLE);
            FoilsDesarmer(FOILS_FIN_IDLE);
        }
        else {
            currentMode = ControlMode::RUN;

            // Armement : on refuse toute consigne tant que le levier
            // n'a pas été vu au neutre au moins une fois.
            // AJOUT : même exigence après une sortie du cruise.
            if ((ARMEMENT_REQUIS && !levierArme) || cruiseRearme) {
                if (pil.au_neutre) {                    // raw <= min calibré + LEVIER_ARME_MARGE
                    levierArme   = true;
                    cruiseRearme = false;
                }
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

#if CRUISE_ACTIF
            // AJOUT : CRUISE. Ici currentRunMode vaut FORWARD seulement
            // si le levier est armé, poussé, et F/R en marche avant.
            uint32_t now_ms = millis();
            bool gps_ok = v_ok && ((now_ms - v_t_ms) <= XSENS_DONNEES_TIMEOUT_MS);

            if (cruiseMode != CRUISE_OFF) {
                // CRUISE → NEUTRAL : consigne 0, levier au neutre exigé
                uint8_t fin = CRUISE_FIN_AUCUNE;
                if (currentRunMode != RunMode::FORWARD) {
                    fin = pil.switch_fr ? CRUISE_FIN_FR : CRUISE_FIN_LEVIER;
                }
                else if (dem & EVT_CRUISE_OFF) {
                    fin = CRUISE_FIN_UI;
                }
                else if ((now_ms - Cruise_vie_ms) > CRUISE_UI_TIMEOUT_MS) {
                    fin = CRUISE_FIN_UI_MUETTE;
                }
                else if (cruiseMode == CRUISE_MODE_VIT && !gps_ok) {
                    fin = CRUISE_FIN_GPS;
                }
                if (fin != CRUISE_FIN_AUCUNE) {
                    CruiseSortir(fin);
                    currentRunMode = RunMode::NEUTRAL;
                    target         = 0;
                }
            }
            else if ((dem & (EVT_CRUISE_ERPM | EVT_CRUISE_VIT)) && !(dem & EVT_CRUISE_OFF)) {
                // FORWARD → CRUISE : demande de l'UI, acceptée seulement en RUN·FORWARD
                uint8_t m = (dem & EVT_CRUISE_ERPM) ? CRUISE_MODE_ERPM : CRUISE_MODE_VIT;
                if (currentRunMode != RunMode::FORWARD) {
                    cruiseFin = CRUISE_REFUS_ETAT;
                }
                else if (m == CRUISE_MODE_VIT && !gps_ok) {
                    cruiseFin = CRUISE_REFUS_GPS;
                }
                else {
                    cruiseMode     = m;
                    cruiseFin      = CRUISE_FIN_AUCUNE;
                    cruiseDebutMs  = now_ms;
                    cruiseBoucleMs = now_ms;
                    cruiseConsigne = (float)target;   // part de la consigne du levier (pas de saut)
                    if (cruiseConsigne < CMD_MIN_UTILE) cruiseConsigne = CMD_MIN_UTILE;
                }
            }

            // En CRUISE : la consigne ne vient plus du levier
            if (cruiseMode != CRUISE_OFF) {
                float dt_s = (float)(now_ms - cruiseBoucleMs) * 0.001f;
                if (dt_s > 0.1f) dt_s = 0.1f;         // passage en retard : pas de saut de consigne
                cruiseBoucleMs = now_ms;
                if (cruiseMode == CRUISE_MODE_ERPM) {
                    cruiseConsigne = CruiseRampe(cruiseConsigne, dt_s);
                } else {
                    cruiseConsigne = CruiseVitesse(cruiseConsigne, v_kmh, erpm_reel, dt_s);
                }
                target = (int)lroundf(cruiseConsigne);
                cruiseDureeMs  = now_ms - cruiseDebutMs;
                currentRunMode = RunMode::CRUISE;
            }
#else
            (void) dem; (void) v_t_ms; (void) erpm_reel;   // cruise désactivé : demandes ignorées
#endif

            // AJOUT : CONTROLE = FORWARD + foils actifs.
            // En CRUISE les foils restent actifs aux mêmes conditions
            // (EVT_CONTROLE) ; l'état affiché reste CRUISE.
            // Bouton Foils de l'UI : armer (pop-up confirmé) / désarmer
            if (demF & EVT_FOILS_OFF) {
                FoilsDesarmer(FOILS_FIN_UI);
            } else if (demF & EVT_FOILS_ON) {
                if (FOILS_ACTIFS) { foilsArme = true;  foilsFin = FOILS_FIN_AUCUNE; }
                else              { foilsFin  = FOILS_REFUS_CONFIG; }
            }

            if ((currentRunMode == RunMode::FORWARD || currentRunMode == RunMode::CRUISE) &&
                ControleAutorise(controlePrec, foilsArme, v_ok, v_kmh, sonars_ok, bits)) {
                controle = true;
                if (currentRunMode == RunMode::FORWARD) currentRunMode = RunMode::CONTROLE;
            }
        }
        // Demande d'armement reçue hors de RUN (IDLE / ARRET) : refusée
        if (currentMode != ControlMode::RUN && (demF & EVT_FOILS_ON) && !(demF & EVT_FOILS_OFF)) {
            foilsFin = FOILS_REFUS_ETAT;
        }
        // Durée en CONTROLE : remise à 0 à l'entrée, figée à la sortie
        {
            uint32_t t = millis();
            if (controle && !controlePrec) controleDebutMs = t;
            if (controle) controleDureeMs = t - controleDebutMs;
        }
        controlePrec = controle;

#if CRUISE_ACTIF
        // Demande de cruise reçue hors de RUN (IDLE / ARRET) : refusée
        if (currentMode != ControlMode::RUN && (dem & (EVT_CRUISE_ERPM | EVT_CRUISE_VIT))) {
            cruiseFin = CRUISE_REFUS_ETAT;
        }
#endif

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
        if (controle)                             actifs |= EVT_CONTROLE;
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
            SM_cruise     = cruiseMode;
            SM_cruise_ms  = cruiseDureeMs;
            SM_cruise_fin = cruiseFin;
            SM_foils_arme  = foilsArme;
            SM_controle    = controle;
            SM_controle_ms = controleDureeMs;
            SM_foils_fin   = foilsFin;
            xSemaphoreGive(dataMutex);
        }

        wd_vivant(T_FSM);
    }
}
