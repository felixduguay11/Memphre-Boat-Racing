#pragma once

// =====================================================
//  task_sdlog.h
//  Journalisation sur la carte SD du Teensy 4.1 (fente intégrée).
//
//  Deux tâches, reliées par une file RAM (message buffer FreeRTOS) :
//
//    Task_SDLog_Echantillon  (PRIO_SDLOG_ECH, 20 Hz)
//      - copie les données partagées sous dataMutex
//      - formate une trame JSON (une ligne) et la pousse dans la file
//      - ne touche JAMAIS à la SD → cadence stable même si la carte rame
//
//    Task_SDLog_Ecriture     (PRIO_SDLOG_ECR, basse)
//      - seule propriétaire de la carte SD
//      - vide la file par blocs, flush toutes les SDLOG_FLUSH_MS
//      - un fichier par passage en RUN : run_0001.jsonl, run_0002.jsonl…
//
//  Format JSON Lines, mêmes conventions que TailleReel (objets imbriqués
//  imu{}, gps{}, son{}, foils{}…) → lisible par UI/analyse/json_graph_ui.py.
//  1re ligne de chaque fichier : {"type":"config", …} (gains et réglages).
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"

enum class SDLogEtat : uint8_t {
    INIT         = 0,   // pas encore initialisée
    PAS_DE_CARTE = 1,   // SD.begin() a échoué (carte absente ?)
    PRET         = 2,   // carte OK, aucun fichier ouvert (hors RUN)
    ENREGISTRE   = 3,   // fichier ouvert, écriture en cours (RUN)
    ERREUR       = 4    // erreur d'ouverture / d'écriture (nouvel essai auto)
};

// ─── État publié (sous dataMutex), lu par Task_RPi ───
// Écrits par Task_SDLog_Ecriture :
extern SDLogEtat SDLog_etat;
extern uint16_t  SDLog_fichier_no;    // n° du fichier courant / dernier (run_XXXX.jsonl)
extern uint32_t  SDLog_lignes;        // trames écrites dans le fichier courant
extern uint32_t  SDLog_sans_fichier;  // trames jetées faute de fichier ouvert (cumul)
extern uint32_t  SDLog_err_ecriture;  // erreurs d'ouverture / d'écriture (cumul)
// Écrits par Task_SDLog_Echantillon :
extern uint32_t  SDLog_perdues;       // trames perdues : file RAM pleine (cumul)
extern float     SDLog_temps_us;      // durée d'un échantillonnage [µs]

// À appeler dans setup(), AVANT xTaskCreate des deux tâches.
void SDLog_init();

void Task_SDLog_Echantillon(void *ptr);
void Task_SDLog_Ecriture(void *ptr);

const char* SDLog_etatStr(SDLogEtat e);
