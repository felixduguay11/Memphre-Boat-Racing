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
    float    dist    = 0.0f;
    float    output  = 0.0f;
    float    cmd     = 0.0f;
    float    t_sonar = 0.0f;
    float    t_ctrl  = 0.0f;
    IMUData  imu;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      dist    = g_distance;
      output  = g_output_pid;
      cmd     = g_cmd_servo;
      t_sonar = g_temps_sonar_us;
      t_ctrl  = g_temps_controle_us;
      imu     = g_imu;
      xSemaphoreGive(dataMutex);
    }

    // =====================================================
    // AFFICHAGE SERIAL
    // =====================================================

    // --- Sonar ---
    Serial.print("[Sonar]    ");
    Serial.print(t_sonar, 1);
    Serial.print(" us  |  Dist=");
    Serial.print(dist, 1);
    Serial.println(" cm");

    // --- Controle foils ---
    Serial.print("[Foils]    ");
    Serial.print(t_ctrl, 1);
    Serial.print(" us  |  Erreur=");
    Serial.print(DISTANCE_REF_CM - dist, 2);
    Serial.print("  Output=");
    Serial.print(output, 2);
    Serial.print("  Cmd=");
    Serial.println(cmd, 1);

    // --- IMU attitude ---
    Serial.print("[IMU]      ");
    Serial.print(imu.temps_us, 1);
    Serial.print(" us  |  ");
    if (imu.att_valid) {
      Serial.print("Roll=");    Serial.print(imu.roll,  2);
      Serial.print("  Pitch="); Serial.print(imu.pitch, 2);
      Serial.print("  Yaw=");   Serial.print(imu.yaw,   2);
      Serial.println(" deg");
    } else {
      Serial.println("attitude invalide");
    }

    // --- IMU vitesse ---
    Serial.print("[Vitesse]          |  ");
    if (imu.vel_valid) {
      Serial.print("Speed="); Serial.print(imu.speed, 2);
      Serial.print(" m/s  vx="); Serial.print(imu.vx, 2);
      Serial.print("  vy=");     Serial.print(imu.vy, 2);
      Serial.print("  vz=");     Serial.println(imu.vz, 2);
    } else {
      Serial.println("vitesse invalide");
    }

    // --- GPS ---
    Serial.print("[GPS]              |  ");
    if (imu.pos_valid) {
      Serial.print("Lat="); Serial.print(imu.lat, 7);
      Serial.print("  Lon="); Serial.println(imu.lon, 7);
    } else {
      Serial.println("position invalide");
    }

    // --- Temps tâche RPi ---
    float duree_us = (float)(micros() - t_debut);
    Serial.print("[RPi]      ");
    Serial.print(duree_us, 1);
    Serial.println(" us");
    Serial.println("------------------------------------------");

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_RPI_MS));
  }
}
