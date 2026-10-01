// =====================================================
//  task_watchdog.cpp
//  Tâche FreeRTOS — sécurité / watchdog, 10 Hz, priorité 5.
// =====================================================

#include "task_watchdog.h"
#include "task_esc_data.h"
#include "task_xsens.h"

// ─── Définition des variables partagées ───
TaskHandle_t      WD_handles[NB_TACHES]   = {};
volatile uint32_t WD_heartbeat[NB_TACHES] = {};

WdTache  WD_taches[NB_TACHES] = {};
uint32_t WD_erreurs    = 0;
bool     WD_surchauffe = false;
bool     WD_reset_wdt  = false;
float    WD_temps_us   = 0.0f;

// ─── Délai max sans battement, par tâche (0 = non surveillée) ───
static const uint32_t TIMEOUT_MS[NB_TACHES] = {
    WD_TIMEOUT_CRITIQUE_MS,   // T_PILOTE
    WD_TIMEOUT_CRITIQUE_MS,   // T_FSM
    WD_TIMEOUT_CRITIQUE_MS,   // T_PROP
    WD_TIMEOUT_CRITIQUE_MS,   // T_ESC
    WD_TIMEOUT_CAPTEUR_MS,    // T_XSENS
    WD_TIMEOUT_CAPTEUR_MS,    // T_SONAR
    WD_TIMEOUT_CAPTEUR_MS,    // T_FOILS
    WD_TIMEOUT_CAPTEUR_MS,    // T_RPI   (info seulement, aucune réaction)
    0                         // T_WD    (surveillé par le RTWDOG)
};

// =====================================================
//  Watchdog matériel : RTWDOG (WDOG3), horloge LPO 32 kHz.
//  Même séquence que la librairie WDT_T4 (tonton81), sans
//  dépendance. Si Task_Watchdog ne le nourrit plus pendant
//  WD_MATERIEL_TIMEOUT_S, le Teensy redémarre (les VESC voient
//  alors la consigne tomber et, au redémarrage, l'armement
//  exige un retour du levier au neutre).
// =====================================================
static void wdt_materiel_begin(float timeout_s)
{
    CCM_CCGR5 |= (3UL << 4);                          // horloge WDOG3
    uint32_t toval = (uint32_t)(timeout_s * 32000.0f);
    if (toval < 32)    toval = 32;
    if (toval > 65535) toval = 65535;                 // ~2 s sans prescaler

    __disable_irq();
    if (WDOG3_CS & WDOG_CS_CMD32EN) {
        WDOG3_CNT = 0xD928C520;                       // déverrouillage
    } else {
        WDOG3_CNT = 0xC520;
        WDOG3_CNT = 0xD928;
    }
    WDOG3_WIN   = 0;
    WDOG3_TOVAL = toval;
    WDOG3_CS    = WDOG_CS_CMD32EN | WDOG_CS_UPDATE | WDOG_CS_CLK(1) |
                  WDOG_CS_FLG | WDOG_CS_EN;
    __enable_irq();
}

static inline void wdt_materiel_feed()
{
    WDOG3_CNT = 0xB480A602;
}

// =====================================================
//  Task_Watchdog — 10 Hz
// =====================================================
void Task_Watchdog(void *ptr)
{
    (void) ptr;

    // Cause du dernier démarrage (bit collant jusqu'à la mise hors tension)
    bool reset_wdt = (SRC_SRSR & SRC_SRSR_WDOG3_RST_B) != 0;

#if WD_MATERIEL_ACTIF
    wdt_materiel_begin(WD_MATERIEL_TIMEOUT_S);
#endif

    uint32_t hb_prec[NB_TACHES];
    uint32_t vu_ms[NB_TACHES];
    uint32_t stack[NB_TACHES] = {};
    uint32_t now = millis();
    for (int i = 0; i < NB_TACHES; i++) {
        hb_prec[i] = WD_heartbeat[i];
        vu_ms[i]   = now;              // période de grâce au démarrage
    }

    // Surchauffe par VESC, avec hystérésis (Real_V1 : MajSurchauffe)
    bool chaud[NB_VESC] = {false, false};

    uint32_t   crit_prec    = 0;
    uint32_t   cycle        = 0;
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (1)
    {
        uint32_t t_debut = micros();
        now = millis();

        // 1) Battements des tâches
        uint32_t erreurs = 0;
        bool     vivante[NB_TACHES];
        for (int i = 0; i < NB_TACHES; i++)
        {
            uint32_t hb = WD_heartbeat[i];
            if (hb != hb_prec[i]) { hb_prec[i] = hb; vu_ms[i] = now; }

            bool presente = (WD_handles[i] != nullptr);
            vivante[i] = true;
            if (presente && TIMEOUT_MS[i] > 0 && (now - vu_ms[i]) > TIMEOUT_MS[i]) {
                vivante[i] = false;
                erreurs |= ERR_TACHE(i);
            }
        }

        // 2) Stack libre (coûteux : 1 fois par seconde)
        if ((cycle % WD_STACK_PERIODE) == 0) {
            for (int i = 0; i < NB_TACHES; i++) {
                if (WD_handles[i] != nullptr) {
                    stack[i] = (uint32_t)uxTaskGetStackHighWaterMark(WD_handles[i]) * sizeof(StackType_t);
                }
            }
        }
        cycle++;

        // 3) Données VESC + Xsens
        EscData  esc[NB_VESC];
        uint32_t xsens_t_ms = 0;
        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            for (int k = 0; k < NB_VESC; k++) esc[k] = ESC_data[k];
            xsens_t_ms = Xsens_data.t_ms;
            xSemaphoreGive(dataMutex);
        }

        // VESC muet : erreur seulement s'il a déjà parlé depuis le démarrage
        // (avant le démarrage des ESC, le silence est normal).
        const uint32_t ERR_VESC[NB_VESC] = {ERR_VESC_A, ERR_VESC_B};
        for (int k = 0; k < NB_VESC; k++) {
            if (esc[k].deja_vu && !esc[k].vivant) erreurs |= ERR_VESC[k];
        }

        // Surchauffe (Real_V1) : un VESC muet garde son dernier état,
        // ses températures sont périmées.
        for (int k = 0; k < NB_VESC; k++) {
            if (!esc[k].vivant) continue;
            float tf = esc[k].t_fet;
            float tm = esc[k].t_mot;
            if (tf >= T_FET_MAX || tm >= T_MOT_MAX) {
                chaud[k] = true;
            } else if (tf < T_FET_MAX - TEMP_HYST && tm < T_MOT_MAX - TEMP_HYST) {
                chaud[k] = false;
            }
            // entre les deux : on garde l'état précédent (hystérésis)
        }
        bool surchauffe = chaud[0] || chaud[1];

        if ((now - xsens_t_ms) > XSENS_DONNEES_TIMEOUT_MS) erreurs |= ERR_XSENS_DONNEES;

        // 4) Bits d'erreur pour la machine d'état et la propulsion
        uint32_t crit = erreurs & ERR_CRITIQUE_MASK;
        EventBits_t actifs = 0;
        if (crit)                           actifs |= EVT_ERR_CRITIQUE;
        if (erreurs & ERR_CAPTEUR_MASK)     actifs |= EVT_ERR_CAPTEUR;
        if (LIMITE_TEMP_ACTIVE && surchauffe) actifs |= EVT_SURCHAUFFE;
        xEventGroupClearBits(egEtat, EVT_WD_MASK & ~actifs);
        if (actifs) xEventGroupSetBits(egEtat, actifs);

        // Réveille la machine d'état tout de suite si l'état critique change
        if (crit != crit_prec) {
            FsmEvent ev;
            ev.type = EvType::ERREUR;
            xQueueSendToFront(qEvents, &ev, 0);
            crit_prec = crit;
        }

        float duree_us = (float)(micros() - t_debut);

        // 5) Publication pour le RPi
        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            for (int i = 0; i < NB_TACHES; i++) {
                WD_taches[i].presente    = (WD_handles[i] != nullptr);
                WD_taches[i].vivante     = vivante[i];
                WD_taches[i].stack_libre = stack[i];
            }
            WD_erreurs    = erreurs;
            WD_surchauffe = surchauffe;
            WD_reset_wdt  = reset_wdt;
            WD_temps_us   = duree_us;
            xSemaphoreGive(dataMutex);
        }

#if WD_MATERIEL_ACTIF
        wdt_materiel_feed();
#endif
        wd_vivant(T_WD);
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_WD_MS));
    }
}
