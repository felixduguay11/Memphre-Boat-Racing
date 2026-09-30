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
//    - surchauffe VESC         → EVT_SURCHAUFFE → consigne plafonnée
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

#define ERR_CRITIQUE_MASK   (ERR_TACHE(T_PILOTE) | ERR_TACHE(T_FSM) | ERR_TACHE(T_PROP) | \
                             ERR_TACHE(T_ESC) | ERR_VESC_A | ERR_VESC_B)
#define ERR_CAPTEUR_MASK    (ERR_TACHE(T_XSENS) | ERR_TACHE(T_SONAR) | ERR_TACHE(T_FOILS) | \
                             ERR_XSENS_DONNEES)

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
extern bool     WD_surchauffe;
extern bool     WD_reset_wdt;     // le dernier démarrage vient du watchdog matériel
extern float    WD_temps_us;

// ─── Tâche FreeRTOS ───
void Task_Watchdog(void *ptr);
