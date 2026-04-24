#pragma once

#include <arduino_freertos.h>
#include <semphr.h>
#include "task_sonar.h"
#include "foils_control.h"
#include "task_imu.h"

// =====================================================
// Paramètres tâche RPi
// =====================================================
const int PERIODE_RPI_MS = 500;  // 2Hz — fréquence d'affichage/envoi

// =====================================================
// Tâche FreeRTOS
//
// Rôle actuel  : affichage Serial de toutes les données
// Rôle futur   : envoi des données au Raspberry Pi via
//                UART (Serial2, pins 7/8 sur Teensy 4.1)
//
// Cette tâche est la SEULE à utiliser Serial
// → zéro contention, pas besoin de serialMutex
// =====================================================
void Task_RPi(void *ptr);
