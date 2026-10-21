#pragma once

// =====================================================
//  task_esc_data.h
//  Tâche FreeRTOS : récupération des données des VESC
//  (priorité 2, 200 Hz) sur CAN3 (pins 30/31, 250 kbps).
//
//  Le bus CAN (FlexCAN_T4 + VescCANBus de Real_V1) est privé
//  à task_esc_data.cpp. Task_Propulsion envoie ses consignes
//  via ESC_EnvoyerConsigne(), protégé par un mutex CAN interne
//  (lecture ici + écriture depuis Propulsion = 2 tâches).
//
//  ⚠ Ne pas inclure VescCAN_Teensy.h / FlexCAN_T4.h ailleurs :
//    ils utilisent des macros Arduino (constrain...) retirées
//    par arduino_freertos.h.
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"
#include "rtos_com.h"

// ─── Données d'un VESC (format des trames JSON de Real_V1) ───
struct EscData {
    uint8_t id;
    bool    vivant;      // trame reçue depuis moins de VESC_TIMEOUT_MS
    bool    deja_vu;     // au moins une trame depuis le démarrage
    int32_t erpm;
    float   duty;
    float   i_mot;
    float   i_in;
    float   v_in;
    float   t_fet;
    float   t_mot;
};

// ─── Qualité du bus CAN (compteurs cumulés depuis le démarrage) ───
struct CanStats {
    uint32_t rx;          // trames lues
    uint32_t mal;         // mal formées (standard, ou STATUS de longueur != 8) → jetées
    uint32_t perdues;     // écrasées dans la mailbox avant lecture (overrun)
    uint32_t inconnues;   // commande non décodée ou ID ≠ 10/11 (info)
    uint32_t tx;          // trames envoyées
    uint32_t tx_echecs;   // envoi refusé (buffer TX plein)
    uint32_t err_bus;     // cycles de 5 ms avec erreur CRC/stuff/forme/bit (ESR1)
    uint32_t err_ack;     // cycles de 5 ms avec erreur ACK (aucun ESC n'acquitte)
    uint8_t  rec, tec;    // compteurs d'erreurs RX / TX du contrôleur (ECR)
    uint8_t  flt;         // 0 erreur-actif, 1 erreur-passif, 2-3 bus off
    float    pct_total;   // % mauvaises (mal + perdues) depuis le démarrage
    float    pct_fenetre; // idem sur la dernière CAN_STATS_FENETRE_MS
};

// ─── Variables partagées (publiées par Task_ESC_Data) ───
extern EscData  ESC_data[NB_VESC];     // [0] = VESC_ID_A, [1] = VESC_ID_B
extern CanStats ESC_can;
extern float    ESC_temps_us;

// ─── Initialisation du bus (dans setup(), avant le scheduler) ───
void ESC_InitCAN();

// ─── Envoi des consignes (appelé par Task_Propulsion) ───
// Unité selon MODE_COMMANDE : eRPM / ‰ de duty / 0,1 A
void ESC_EnvoyerConsigne(int32_t cmd_a, int32_t cmd_b);

// ─── Tâche FreeRTOS ───
void Task_ESC_Data(void *ptr);
