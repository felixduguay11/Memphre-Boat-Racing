#pragma once

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"

// Variables sonars
extern float distance_filt[NB_CANAUX];
extern int PINS_SONARS[NB_CANAUX];
extern int PINS_SONARS_IO[NB_CANAUX];

// Variables partagées
extern float Sonar_distance[NB_CANAUX];
extern float Sonar_temps_us;
extern bool  Sonar_io[NB_CANAUX];      // sortie TOR du sonar : true = valide

// ─── Zéro sur l'eau (UI : Calibration → Sonars) ───
// Commande du Pi {"cmd":"sonar_zero"} → SonarZero_Demander() (Task_RPi).
// Task_LectureSonar fait la moyenne de chaque sonar pendant
// SONAR_ZERO_DUREE_MS ; si tout est bon, le zéro est sauvé en EEPROM.
// Consigne des foils = zéro + H_VOL_x_CM ; CONTROLE refusé sans zéro.
enum SonarZeroRes : uint8_t {
    SZ_AUCUN       = 0,
    SZ_OK          = 1,   // zéro enregistré
    SZ_REFUS_ON    = 2,   // refusé : switch ON actif
    SZ_ANNULE_ON   = 3,   // arrêté : switch ON activé pendant la mesure
    SZ_INVALIDE    = 4,   // sortie TOR « non valide » pendant la mesure
    SZ_AGITE       = 5,   // une lecture s'écarte de plus de SONAR_ZERO_ECART_MAX_CM
    SZ_HORS_PLAGE  = 6,   // mesure saturée, ou zéro + hauteur de vol > SONAR_DIST_MAX_CM
    SZ_SONARS_OFF  = 7,   // sonars pas lus (SONARS_ACTIFS = 0)
};

struct SonarZeroEtat {
    bool     actif;                 // mesure en cours
    uint16_t reste_ms;              // temps restant de la mesure
    uint8_t  res;                   // SonarZeroRes de la dernière mesure
    uint8_t  res_s[NB_CANAUX];      // résultat de chaque sonar
    float    moy[NB_CANAUX];        // dernière mesure : moyenne (cm)
    float    ecart[NB_CANAUX];      //                   plus grand écart à la moyenne (cm)
};

extern float         Sonar_zero[NB_CANAUX];   // cm (valide si Sonar_zero_ok)
extern bool          Sonar_zero_ok;
extern SonarZeroEtat Sonar_zero_etat;
const float H_VOL[NB_CANAUX] = {H_VOL_AVANT_CM, H_VOL_ARRIERE_GAUCHE_CM, H_VOL_ARRIERE_DROIT_CM};

void Sonars_ChargerZero();                    // setup(), avant les tâches (EEPROM)
void SonarZero_Demander(bool switch_on);      // Task_RPi : commande "sonar_zero"

// Fonction
float lire_sonar(int i, float &filt, float &brute);

// Tâche FreeRTOS
void Task_LectureSonar(void *ptr);
