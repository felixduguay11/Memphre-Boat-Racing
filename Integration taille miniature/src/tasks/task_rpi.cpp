#include "task_rpi.h"

// Mutex défini dans main.cpp
extern SemaphoreHandle_t dataMutex;

// =====================================================
// Task_RPi — priorité 1 (basse), période 500ms
//
// PHASE ACTUELLE : affichage Serial uniquement
//
// Les blocs d'affichage sont conditionnés par les flags
// RUN_SONAR / RUN_FOILS / RUN_XSENS (voir config.h) afin
// de n'afficher que les tâches réellement actives pendant
// les tests unitaires.
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

    // --- Champs Xsens supplémentaires (test unitaire) ---
    bool     att_valid            = false;
    bool     pos_valid            = false;
    bool     vel_valid            = false;
    double   lat                  = 0.0;
    double   lon                  = 0.0;
    float    speed                = 0.0f;
    float    xsens_us             = 0.0f;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      dist_avant            = Sonar_distance[0];
      dist_arriere_gauche   = Sonar_distance[1];
      dist_arriere_droit    = Sonar_distance[2];

      output_avant          = H_outputs[0];
      output_arriere_gauche = H_outputs[1];
      output_arriere_droit  = H_outputs[2];

      cmd_avant             = HPR_cmd_servos[0];
      cmd_arriere_gauche    = HPR_cmd_servos[1];
      cmd_arriere_droit     = HPR_cmd_servos[2];

      temps_tache_sonar     = Sonar_temps_us;
      temps_tache_H_ctrl    = HPR_control_time_us;

      roll                  = Xsens_data.roll;
      pitch                 = Xsens_data.pitch;
      yaw                   = Xsens_data.yaw;

      att_valid             = Xsens_data.att_valid;
      pos_valid             = Xsens_data.pos_valid;
      vel_valid             = Xsens_data.vel_valid;
      lat                   = Xsens_data.lat;
      lon                   = Xsens_data.lon;
      speed                 = Xsens_data.speed;
      xsens_us              = Xsens_data.temps_us;

      xSemaphoreGive(dataMutex);
    }

    // =====================================================
    // AFFICHAGE SERIAL
    // =====================================================

#if RUN_SONAR
    // --- Sonar ---
    Serial.print("[Sonar]    ");
    Serial.print(temps_tache_sonar, 1);
    Serial.print(" us  |  Dist=");
    Serial.print(dist_avant, 1);
    Serial.println(" cm");
#endif

#if RUN_FOILS
    // --- Controle foils ---
    Serial.print("[Foils]    ");
    Serial.print(temps_tache_H_ctrl, 1);
    Serial.print(" us  |  Erreur=");
    Serial.print(H_DISTANCE_REF_AVANT - dist_avant, 2);
    Serial.print("  Output=");
    Serial.print(output_arriere_gauche, 2);
    Serial.print("  Cmd=");
    Serial.println(cmd_arriere_gauche, 1);
#endif

#if RUN_XSENS
    // --- IMU attitude ---
    Serial.print("[Xsens] valid A/P/V=");
    Serial.print(att_valid); Serial.print("/");
    Serial.print(pos_valid); Serial.print("/");
    Serial.print(vel_valid);
    Serial.print("  Roll=");   Serial.print(roll,  1);
    Serial.print(" Pitch=");   Serial.print(pitch, 2);
    Serial.print(" Yaw=");     Serial.print(yaw,   2);
    Serial.print(" deg | Speed="); Serial.print(speed, 2);
    Serial.print(" m/s | t=");     Serial.print(xsens_us, 1);
    Serial.println(" us");
    Serial.print("        Lat="); Serial.print(lat, 6);
    Serial.print(" Lon=");        Serial.println(lon, 6);
#endif

    // --- Temps tâche RPi ---
    float duree_us = (float)(micros() - t_debut);
    Serial.print("[RPi]      ");
    Serial.print(duree_us, 1);
    Serial.println(" us");
    Serial.println("------------------------------------------");

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_RPI_MS));
  }
}