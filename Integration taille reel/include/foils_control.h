#pragma once

#include <arduino_freertos.h>
#include <semphr.h>
#include <PWMServo.h>

// =====================================================
// Paramètres servo
// =====================================================
const int   SERVO_NEUTRAL    = 148;
const int   SERVO_MIN        = 120;
const int   SERVO_MAX        = 163;
const float ALPHA_SERVO      = 0.2f;
const int   PIN_SERVO        = 9;
const int   PERIODE_CTRL_MS  = 20;  // 50Hz

// =====================================================
// Paramètres PID Fuzzy
// =====================================================
const float DISTANCE_REF_CM  = 70.0f;  // consigne hauteur
const float ALPHA_DERIVEE    = 0.15f;  // lissage dérivée
const float ZONE_MORTE_DERIV = 0.3f;   // cm — ignore bruit résiduel
const float ZONE_MORTE_ERR   = 1.0f;   // cm — deadband erreur

// =====================================================
// Variables partagées — écrites par Task_FoilsControl
// Lues par Task_Debug via dataMutex
// =====================================================
extern float g_output_pid;
extern float g_cmd_servo;
extern float g_temps_controle_us;

// =====================================================
// Tâche FreeRTOS
// =====================================================
void Task_FoilsControl(void *ptr);
