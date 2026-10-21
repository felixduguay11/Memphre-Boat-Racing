#pragma once

// =====================================================
//  rtos_com.h — communication inter-tâches (Architecture2.png)
//
//   Task_Pilote ───(qEvents : snapshot pilote)────► Task_StateMachine
//   Task_Watchdog ─(qEvents : EvType::ERREUR)─────► Task_StateMachine
//   Task_StateMachine ─(qCmd : mailbox, 1 place)──► Task_Propulsion
//   Task_StateMachine ─(egEtat : bits d'état)─────► Propulsion, Foils
//   Task_Watchdog ─────(egEtat : bits d'erreur)───► StateMachine, Propulsion
//   Task_RPi ──────────(egEtat : EVT_CALIB_*)─────► Pilote (calibration levier)
//   Tâches ───────────(Shared Data, dataMutex)────► RPi, Watchdog, FSM
//
//  qCmd est une « mailbox » : longueur 1, écrite par xQueueOverwrite()
//  et lue par xQueuePeek() → plusieurs lecteurs possibles, toujours
//  la dernière commande.
//
//  Les objets sont créés dans main.cpp avant vTaskStartScheduler().
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include <queue.h>
#include <event_groups.h>
#include "config.h"

// ─── États (noms de Real_V1 + ajouts ARRET / CONTROLE) ───
enum class ControlMode : uint8_t { IDLE = 0, RUN = 1, ARRET = 2 };
enum class RunMode     : uint8_t { NEUTRAL = 0, FORWARD = 1, REVERSE = 2, CONTROLE = 3 };

// ─── Snapshot des entrées pilote (publié par Task_Pilote) ───
struct PiloteData {
    bool switch_on;      // I_Switch_ON actif  → RUN
    bool switch_fr;      // I_F_R actif        → marche arrière
    bool switch_ctl;     // Relay_Control actif → contrôle foils autorisé
    bool esc_demarres;   // I_But_Start actif  → ESC/moteurs démarrés
    int  levier_raw;     // ADC brut (10 bits)
    int  levier;         // levier nettoyé 0..1023 (0 = repos / deadband)
    int  target_fwd;     // consigne si marche avant  (0 ou CMD_MIN_UTILE..CMD_MAX_FORWARD)
    int  target_rev;     // consigne si marche arrière (0 ou -CMD_MIN_UTILE..-CMD_MAX_REVERSE)
                         // unité selon MODE_COMMANDE : eRPM / ‰ duty / 0,1 A
    // ── Ajouts (en fin de struct : les initialisations courtes restent valides) ──
    bool    au_neutre;   // levier sous le seuil d'armement (relatif au min calibré)
    bool    dcdc;        // sortie DC_DC_3V3 active (OFF 3 s après I_But_Start ON)
    uint8_t sw_brut;     // lecture brute des switchs avant anti-rebond (bits : 0 ON, 1 F/R, 2 CTL, 3 START)
    uint8_t calib;       // 1 = calibration en cours (UI), 2 = calibration auto (commande secrète)
    uint8_t cal_res;     // dernier résultat : 0 aucun, 1 OK, 2 course trop courte,
                         //   3 levier pas revenu au repos, 4 annulée (UI / ON / timeout),
                         //   5 refusée (switch ON actif)
    int16_t lev_min;     // bornes du levier en service (calibration ou config.h)
    int16_t lev_max;
    int16_t cal_min;     // min / max vus pendant la calibration en cours
    int16_t cal_max;
};

// ─── Événements reçus par la machine d'état (qEvents) ───
enum class EvType : uint8_t { PILOTE = 0, ERREUR = 1 };

struct FsmEvent {
    EvType     type;
    PiloteData pilote;   // valide si type == PILOTE
};

// ─── Commande publiée par la machine d'état (qCmd) ───
struct FsmCmd {
    ControlMode mode;
    RunMode     run;
    int32_t     target;  // consigne signée (unité de MODE_COMMANDE), avant rampe
    TickType_t  tick;    // xTaskGetTickCount() à la publication
};

// ─── Bits de l'event group egEtat ───
// Écrits par la machine d'état
#define EVT_RUN            (1UL << 0)
#define EVT_CONTROLE       (1UL << 1)   // foils actifs (PID)
#define EVT_ARRET          (1UL << 2)
#define EVT_FSM_MASK       (EVT_RUN | EVT_CONTROLE | EVT_ARRET)
// Écrits par le watchdog
#define EVT_ERR_CRITIQUE   (1UL << 8)   // → ARRET, consigne 0
#define EVT_ERR_CAPTEUR    (1UL << 9)   // → pas de CONTROLE
#define EVT_SURCHAUFFE     (1UL << 10)  // → consigne plafonnée
#define EVT_WD_MASK        (EVT_ERR_CRITIQUE | EVT_ERR_CAPTEUR | EVT_SURCHAUFFE)
// Écrits par Task_RPi (commandes du Pi), lus et effacés par Task_Pilote
#define EVT_CALIB_DEBUT    (1UL << 16)  // {"cmd":"calib"}
#define EVT_CALIB_FIN      (1UL << 17)  // {"cmd":"calib_fin"}
#define EVT_CALIB_ANNULE   (1UL << 18)  // {"cmd":"calib_annule"}
#define EVT_CALIB_MASK     (EVT_CALIB_DEBUT | EVT_CALIB_FIN | EVT_CALIB_ANNULE)

// ─── Objets FreeRTOS (définis dans main.cpp) ───
extern SemaphoreHandle_t  dataMutex;
extern SemaphoreHandle_t  adcMutex;
extern QueueHandle_t      qEvents;
extern QueueHandle_t      qCmd;
extern EventGroupHandle_t egEtat;

// ─── Lecture ADC partagée ───
// analogRead() est appelé par Task_Pilote ET Task_LectureSonar (même
// priorité) : sans protection, une conversion peut être écrasée par
// l'autre tâche. analogRead() appelle yield() pendant la conversion,
// donc on utilise un mutex plutôt que vTaskSuspendAll().
static inline int adc_lire(int pin)
{
    int v = 0;
    if (xSemaphoreTake(adcMutex, portMAX_DELAY)) {
        v = analogRead(pin);
        xSemaphoreGive(adcMutex);
    }
    return v;
}
