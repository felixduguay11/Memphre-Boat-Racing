#pragma once
// =====================================================
//  servos.h — sortie PWM des 3 servos des foils, en µs,
//  et leur calibration Min / Neutre / Max (EEPROM).
//
//  Servos : 0 avant (pin 2), 1 arrière gauche (pin 3),
//           2 arrière droit (pin 4) — même ordre que NB_CANAUX.
//
//  Qui commande les servos :
//    - Task_foils_Control (si FOILS_ACTIFS), hors calibration ;
//    - Task_RPi pendant une calibration (commandes "servo_*" du Pi).
//  Un servo sans calibration valide n'est jamais commandé, sauf
//  pendant sa propre calibration.
//
//  Commandes du Pi (JSON, une par ligne) :
//    {"cmd":"servo_calib","s":N}   début (ou changement) : servo N
//                                  refusé si le switch ON est actif
//    {"cmd":"servo_pas","d":D}     déplace de D µs (|D| ≤ SERVO_CALIB_PAS_MAX)
//    {"cmd":"servo_enr","k":"min"} enregistre la position (min / neutre / max)
//    {"cmd":"servo_fin"}           fin : le servo va à son neutre s'il est valide
//    {"cmd":"servo_vie"}           signe de vie (toute commande servo_* en vaut un)
//  Fin automatique : switch ON actif, ou pas de signe de vie depuis
//  SERVO_CALIB_VIE_MS.
// =====================================================
#include <arduino_freertos.h>
#include "config.h"

enum ServoVal : uint8_t { SV_MIN = 0, SV_NEUTRE = 1, SV_MAX = 2 };

// Dernier résultat de calibration (télémétrie sv.r)
enum ServoCalRes : uint8_t {
    SCR_AUCUN      = 0,
    SCR_ENREGISTRE = 1,   // une valeur vient d'être enregistrée
    SCR_REFUS_ON   = 2,   // début refusé : switch ON actif
    SCR_FIN        = 3,   // fin demandée par l'UI
    SCR_FIN_ON     = 4,   // fin : switch ON activé pendant la calibration
    SCR_FIN_VIE    = 5,   // fin : plus de signe de vie de l'UI
};

// Pour la télémétrie
struct ServoEtat {
    int8_t   actif;                    // servo en calibration, -1 si aucun
    uint16_t us;                       // position commandée pendant la calibration
    uint8_t  res;                      // ServoCalRes
    uint8_t  derniere;                 // ServoVal de la dernière valeur enregistrée
    uint16_t cal[NB_CANAUX][3];        // Min / Neutre / Max (0 = pas enregistré)
    bool     valide[NB_CANAUX];
};

void Servos_Init();                    // setup(), avant les tâches : PWM + EEPROM

// Calibration en service (copie sous mutex)
bool Servo_Valide(int i);
bool Servos_TousValides();
bool Servo_Bornes(int i, float &mn, float &neutre, float &mx);   // false si pas valide

// Sortie PWM (bornée à SERVO_US_ABS_MIN..MAX ; l'appelant borne à Min / Max)
void Servo_Ecrire(int i, float us);
void Servo_Couper(int i);              // plus d'impulsion

// Calibration (appelé par Task_RPi)
bool ServoCalib_Active();
void ServoCalib_Commande(const char* ligne, bool switch_on);
void ServoCalib_Tick(bool switch_on);  // à chaque cycle de Task_RPi
void ServoCalib_Etat(ServoEtat &e);
