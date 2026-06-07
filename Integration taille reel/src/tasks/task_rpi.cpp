#include "task_rpi.h"

// Mutex défini dans main.cpp
extern SemaphoreHandle_t dataMutex;

// =====================================================
// Task_RPi — priorité 1 (basse), période 500ms
//
// PHASE ACTUELLE : affichage Serial uniquement
//
// PHASE FUTURE : remplacer les Serial.print() par
//   Serial2.print() pour envoyer au Raspberry Pi
//   via UART (pins 7=RX2, 8=TX2 sur Teensy 4.1)
//   Ajouter dans setup() : Serial2.begin(115200);
// =====================================================
void Task_RPi(void *ptr)
{
  (void) ptr;
  TickType_t lastWakeTime = xTaskGetTickCount();

  while (1)
  {
    uint32_t t_debut = micros();

    // --- Copie locale de toutes les données sous mutex ---
    float    dist_avant           = 0.0f;
    float    dist_arriere_gauche  = 0.0f;
    float    dist_arriere_droit   = 0.0f;

    float    output_avant         = 0.0f;
    float    output_arriere_gauche= 0.0f;
    float    output_arriere_droit = 0.0f;

    float    cmd_avant            = 0.0f;
    float    cmd_arriere_gauche   = 0.0f;
    float    cmd_arriere_droit    = 0.0f;

    float    temps_tache_sonar    = 0.0f;
    float    temps_tache_H_ctrl   = 0.0f;

    float    roll                 = 0.0f;
    float    pitch                = 0.0f;
    float    yaw                  = 0.0f;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      dist_avant            = Sonar_distance[0];
      dist_arriere_gauche   = Sonar_distance[1];
      dist_arriere_droit    = Sonar_distance[2];

      output_avant          = H_outputs[0];
      output_arriere_gauche = H_outputs[1];
      output_arriere_droit  = H_outputs[2];

      cmd_avant             = H_cmd_servos[0];
      cmd_arriere_gauche    = H_cmd_servos[1];
      cmd_arriere_droit     = H_cmd_servos[2];

      temps_tache_sonar     = Sonar_temps_us;
      temps_tache_H_ctrl    = H_control_time_us;

      roll                  = Xsens_data.roll;
      pitch                 = Xsens_data.pitch;
      yaw                   = Xsens_data.yaw;

      xSemaphoreGive(dataMutex);
    }

    // =====================================================
    // AFFICHAGE SERIAL
    // =====================================================

    // --- Sonar ---
    Serial.print("[Sonar]    ");
    Serial.print(temps_tache_sonar, 1);
    Serial.print(" us  |  Dist=");
    Serial.print(dist_avant, 1);
    Serial.println(" cm");

    // --- Controle foils ---
    Serial.print("[Foils]    ");
    Serial.print(temps_tache_H_ctrl, 1);
    Serial.print(" us  |  Erreur=");
    Serial.print(H_DISTANCE_REF_AVANT - dist_avant, 2);
    Serial.print("  Output=");
    Serial.print(output_avant, 2);
    Serial.print("  Cmd=");
    Serial.println(cmd_avant, 1);

    // --- IMU attitude ---
    Serial.print("[Xsens]      ");
    Serial.print("Roll=");    Serial.print(roll,  1);
    Serial.print("  Pitch="); Serial.print(pitch, 2);
    Serial.print("  Yaw=");   Serial.print(yaw,   2);
    Serial.println(" deg");

    // --- Temps tâche RPi ---
    float duree_us = (float)(micros() - t_debut);
    Serial.print("[RPi]      ");
    Serial.print(duree_us, 1);
    Serial.println(" us");
    Serial.println("------------------------------------------");

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_RPI_MS));
  }
}
