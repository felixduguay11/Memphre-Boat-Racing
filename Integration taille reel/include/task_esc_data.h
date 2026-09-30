#pragma once

// =====================================================
//  task_esc_data.h
//  Tâche FreeRTOS : récupération des données des VESC
//  (priorité 2, 1 kHz) sur CAN3 (pins 30/31, 250 kbps).
//
//  Le bus CAN (FlexCAN_T4 + VescCANBus de Real_V1) est privé
//  à task_esc_data.cpp. Task_Propulsion envoie ses consignes
//  via ESC_EnvoyerERPM(), protégé par un mutex CAN interne
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

// ─── Variables partagées (publiées par Task_ESC_Data) ───
extern EscData ESC_data[NB_VESC];     // [0] = VESC_ID_A, [1] = VESC_ID_B
extern float   ESC_temps_us;

// ─── Initialisation du bus (dans setup(), avant le scheduler) ───
void ESC_InitCAN();

// ─── Envoi des consignes (appelé par Task_Propulsion) ───
void ESC_EnvoyerERPM(int32_t erpm_a, int32_t erpm_b);

// ─── Tâche FreeRTOS ───
void Task_ESC_Data(void *ptr);
