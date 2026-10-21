#pragma once

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"
#include "task_sonar.h"
#include "task_foils_control.h"
#include "task_xsens.h"

// =====================================================
//  Task_RPi — liaison Raspberry Pi (priorité 1, 20 Hz).
//  Seule tâche qui écrit/lit sur Serial (USB = /dev/ttyACM0).
//  Protocole de Real_V1 :
//    Pi → Teensy : {"cmd":"start"} / {"cmd":"stop"}
//                  {"cmd":"calib"} / {"cmd":"calib_fin"} / {"cmd":"calib_annule"}
//    Teensy → Pi : télémétrie JSON à 20 Hz si streaming,
//                  sinon {"type":"hb",...} toutes les HB_MS.
// =====================================================

// Tache affichage
void Task_RPi(void *ptr);
