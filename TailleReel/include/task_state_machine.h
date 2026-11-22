#pragma once

// =====================================================
//  task_state_machine.h
//  Tâche FreeRTOS : machine d'état (priorité 4).
//
//  Purement réactive : bloquée sur qEvents (snapshot pilote
//  à 50 Hz + événements d'erreur du watchdog).
//
//  Logique = Real_V1 :
//    IDLE : switch ON relâché → consigne 0, armement perdu
//    RUN  : armement (levier vu au neutre), puis
//           F/R actif → REVERSE, sinon FORWARD ;
//           consigne 0 → NEUTRAL
//  Ajouts :
//    ARRET    : erreur critique du watchdog → consigne 0,
//               armement perdu (retour levier au neutre exigé)
//    CONTROLE : sous-état de FORWARD, foils actifs (voir config.h)
//    CRUISE   : sous-état de RUN, cruise control (voir config.h).
//               Demandé par l'UI (EVT_CRUISE_ERPM / EVT_CRUISE_VIT) :
//                 FORWARD → CRUISE   seulement depuis RUN·FORWARD
//                 CRUISE  → NEUTRAL  levier au repos, bouton de l'UI,
//                                    F/R, UI muette, vitesse GPS
//                                    invalide (cruise vitesse) ;
//                                    consigne 0 + levier au neutre
//                                    exigé avant de repartir
//                 CRUISE  → IDLE / ARRET  comme les autres états
//               La consigne ne vient plus du levier : CRUISE_ERPM
//               (cruise ERPM, rejoint avec une rampe) ou boucle sur la
//               vitesse GPS (cruise vitesse). Les foils restent actifs aux mêmes
//               conditions que CONTROLE (EVT_CONTROLE).
//
//  Sorties : qCmd (mailbox) → Propulsion,
//            egEtat (EVT_RUN / EVT_CONTROLE / EVT_ARRET),
//            Shared Data (SM_*) → RPi.
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"
#include "rtos_com.h"

// ─── Cruise control ───
// Mode (SM_cruise, télémétrie "cr.m")
#define CRUISE_OFF          0
#define CRUISE_MODE_ERPM    1      // consigne tenue à CRUISE_ERPM
#define CRUISE_MODE_VIT     2      // vitesse GPS tenue à CRUISE_VITESSE_KMH
// Raison de la dernière sortie ou du dernier refus (SM_cruise_fin,
// télémétrie "cr.fin") — mêmes numéros que CRUISE_FINS de l'UI
#define CRUISE_FIN_AUCUNE    0
#define CRUISE_FIN_UI        1     // case Cruise control de l'UI (ou Stop)
#define CRUISE_FIN_LEVIER    2     // levier ramené au repos
#define CRUISE_FIN_FR        3     // switch F/R
#define CRUISE_FIN_GPS       4     // vitesse GPS invalide ou périmée (cruise vitesse)
#define CRUISE_FIN_UI_MUETTE 5     // plus de signe de vie de l'UI
#define CRUISE_FIN_IDLE      6     // switch ON à OFF
#define CRUISE_FIN_ARRET     7     // erreur critique
#define CRUISE_REFUS_ETAT    8     // demande refusée : pas en RUN·FORWARD
#define CRUISE_REFUS_GPS     9     // demande refusée : vitesse GPS invalide

// ─── Bouton Foils de l'UI (armement du contrôle des foils) ───
// Dernier événement (SM_foils_fin, télémétrie "fc.fin") — mêmes numéros
// que FOILS_FINS de l'UI
#define FOILS_FIN_AUCUNE     0
#define FOILS_FIN_UI         1     // désarmé par le bouton Foils (ou Stop)
#define FOILS_FIN_IDLE       2     // désarmé : switch ON à OFF
#define FOILS_FIN_ARRET      3     // désarmé : erreur critique
#define FOILS_REFUS_ETAT     4     // armement refusé : pas en RUN
#define FOILS_REFUS_CONFIG   5     // armement refusé : FOILS_ACTIFS à 0

// ─── Variables partagées (publiées par Task_StateMachine) ───
extern ControlMode SM_mode;
extern RunMode     SM_run;
extern int         SM_target;      // consigne (unité de MODE_COMMANDE) avant rampe / plafond
extern bool        SM_arme;        // levier armé
extern float       SM_temps_us;
extern uint8_t     SM_cruise;      // CRUISE_OFF / CRUISE_MODE_ERPM / CRUISE_MODE_VIT
extern uint32_t    SM_cruise_ms;   // durée de l'activation (figée à la sortie, remise à 0 à l'entrée)
extern uint8_t     SM_cruise_fin;  // CRUISE_FIN_* / CRUISE_REFUS_*
extern bool        SM_foils_arme;  // bouton Foils confirmé
extern bool        SM_controle;    // foils actifs (CONTROLE, aussi en CRUISE)
extern uint32_t    SM_controle_ms; // durée en CONTROLE (figée à la sortie, remise à 0 à l'entrée)
extern uint8_t     SM_foils_fin;   // FOILS_FIN_* / FOILS_REFUS_*

// millis() du dernier signe de vie de l'UI (toute commande "cruise_*").
// Écrit par Task_RPi, lu par la machine d'état (32 bits : atomique).
extern volatile uint32_t Cruise_vie_ms;

// ─── Tâche FreeRTOS ───
void Task_StateMachine(void *ptr);
