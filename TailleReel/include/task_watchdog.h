#pragma once

// =====================================================
//  task_watchdog.h
//  Tâche FreeRTOS : sécurité / watchdog (priorité 5).
//
//  Surveille :
//    - le battement de cœur de chaque tâche (wd_vivant())
//    - le stack libre de chaque tâche
//    - les VESC : muet > VESC_TIMEOUT_MS, surchauffe (Real_V1)
//    - la fraîcheur des données Xsens
//
//  Réactions :
//    - tâche critique figée (Pilote, FSM, Propulsion, ESC) ou VESC
//      muet  → EVT_ERR_CRITIQUE → machine d'état en ARRET, consigne 0
//    - Xsens / Sonar / Foils figés ou Xsens périmé
//                              → EVT_ERR_CAPTEUR → pas de CONTROLE
//    - températures FET / moteur de chaque VESC, 2 niveaux (config.h) :
//        attention → WD_temp_niv seulement (avertissement à l'écran)
//        limite    → EVT_SURCHAUFFE → consigne plafonnée (si LIMITE_TEMP_ACTIVE)
//        capteur invalide → WD_temp_niv seulement (jamais de plafond)
//    - tension de la batterie secondaire 4S (pin V_batt_secondaire) :
//        < V_BATT_SEC_ATTENTION → WD_batt_sec_niv (avertissement à l'écran)
//        < V_BATT_SEC_ARRET pendant V_BATT_SEC_ARRET_MS
//                          → ERR_BATT_SEC (critique, verrouillée jusqu'au
//                            redémarrage) → ARRET, consigne 0
//        mesure impossible → WD_batt_sec_niv seulement (jamais d'arrêt)
//    - watchdog matériel (RTWDOG) nourri à chaque cycle : si cette
//      tâche ne tourne plus, le Teensy redémarre.
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"
#include "rtos_com.h"

// ─── Identifiants des tâches surveillées ───
enum TacheId : uint8_t {
    T_PILOTE = 0,
    T_FSM,
    T_PROP,
    T_ESC,
    T_XSENS,
    T_SONAR,
    T_FOILS,
    T_RPI,
    T_WD,          // lui-même : stack seulement (surveillé par le RTWDOG)
    NB_TACHES
};

// ─── Bits d'erreur (WD_erreurs) ───
#define ERR_TACHE(id)       (1UL << (id))
#define ERR_VESC_A          (1UL << 16)
#define ERR_VESC_B          (1UL << 17)
#define ERR_XSENS_DONNEES   (1UL << 18)
#define ERR_BATT_SEC        (1UL << 19)   // batterie secondaire vide (verrouillée)

#define ERR_CRITIQUE_MASK   (ERR_TACHE(T_PILOTE) | ERR_TACHE(T_FSM) | ERR_TACHE(T_PROP) | \
                             ERR_TACHE(T_ESC) | ERR_VESC_A | ERR_VESC_B | ERR_BATT_SEC)
#define ERR_CAPTEUR_MASK    (ERR_TACHE(T_XSENS) | ERR_TACHE(T_SONAR) | ERR_TACHE(T_FOILS) | \
                             ERR_XSENS_DONNEES)

// ─── Niveau de température d'un capteur (WD_temp_niv, télémétrie "tn") ───
enum TempNiveau : uint8_t {
    TEMP_OK        = 0,
    TEMP_ATTENTION = 1,    // >= T_*_ATTENTION : avertissement
    TEMP_LIMITE    = 2,    // >= T_*_MAX : consigne plafonnée
    TEMP_CAPTEUR   = 3     // valeur impossible : capteur débranché / non configuré
};
#define TEMP_FET  0        // index dans WD_temp_niv[k][]
#define TEMP_MOT  1
#define NB_TEMP   2

// ─── Niveau de la batterie secondaire (WD_batt_sec_niv, télémétrie "b12") ───
enum BattSecNiveau : uint8_t {
    BATT_SEC_OK        = 0,
    BATT_SEC_ATTENTION = 1,    // < V_BATT_SEC_ATTENTION (ou compte à rebours avant l'arrêt)
    BATT_SEC_ARRET     = 2,    // arrêt verrouillé jusqu'au redémarrage
    BATT_SEC_INVALIDE  = 3     // mesure impossible
};

// ─── État d'une tâche (publié pour le RPi) ───
struct WdTache {
    bool     presente;     // tâche créée
    bool     vivante;      // battement reçu dans le délai
    uint32_t stack_libre;  // octets jamais utilisés (high water mark)
};

// ─── Handles et battements (remplis par main.cpp / les tâches) ───
extern TaskHandle_t      WD_handles[NB_TACHES];
extern volatile uint32_t WD_heartbeat[NB_TACHES];

// Appelée par chaque tâche à chaque cycle (un seul écrivain par case)
static inline void wd_vivant(TacheId id) { WD_heartbeat[id] = WD_heartbeat[id] + 1; }

// ─── Variables partagées (publiées par Task_Watchdog) ───
extern WdTache  WD_taches[NB_TACHES];
extern uint32_t WD_erreurs;       // combinaison de ERR_*
extern bool     WD_surchauffe;    // au moins un capteur au niveau TEMP_LIMITE
extern uint8_t  WD_temp_niv[NB_VESC][NB_TEMP];   // TempNiveau, par VESC : FET, moteur
extern float    WD_batt_sec_v;    // V : tension moyennée de la batterie secondaire
extern uint8_t  WD_batt_sec_niv;  // BattSecNiveau
extern uint32_t WD_batt_sec_reste_ms;  // compte à rebours avant l'arrêt (0 = aucun)
extern bool     WD_reset_wdt;     // le dernier démarrage vient du watchdog matériel
extern float    WD_temps_us;

// ─── Tâche FreeRTOS ───
void Task_Watchdog(void *ptr);
