#include "task_rpi.h"
#include "task_propulsion.h"      // MODIF : snapshot manette (Prop_*)
#include "task_state_machine.h"   // MODIF : état machine (SM_*) + enums

// Mutex défini dans main.cpp
extern SemaphoreHandle_t dataMutex;

// MODIF : helpers d'affichage des états de la machine d'état
static const char* topStateStr(TopState s)
{
    switch (s) {
        case TopState::IDLE: return "IDLE";
        case TopState::RUN:  return "RUN";
        case TopState::STOP: return "STOP";
    }
    return "?";
}

static const char* runStateStr(RunState s)
{
    switch (s) {
        case RunState::NEUTRE:   return "NEUTRE";
        case RunState::AVANCE:   return "AVANCE";
        case RunState::RECULE:   return "RECULE";
        case RunState::CONTROLE: return "CONTROLE";
    }
    return "?";
}

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

    // MODIF : télémétrie machine d'état + propulsion
    TopState sm_top               = TopState::IDLE;
    RunState sm_run               = RunState::NEUTRE;
    bool     sm_foils             = false;
    float    temps_tache_sm       = 0.0f;

    float    rc_throttle          = 0.0f;
    float    rc_rudder            = 0.0f;
    bool     rc_switchA           = false;
    bool     rc_switchC           = false;
    bool     rc_valid             = false;
    float    temps_tache_prop     = 0.0f;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      dist_avant            = Sonar_distance[0];
      dist_arriere_gauche   = Sonar_distance[1];
      //dist_arriere_droit    = Sonar_distance[2];

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

      // MODIF : machine d'état
      sm_top                = SM_top_state;
      sm_run                = SM_run_state;
      sm_foils              = SM_foil_control_active;
      temps_tache_sm        = SM_control_time_us;

      // MODIF : propulsion / manette
      rc_throttle           = Prop_rc_throttle;
      rc_rudder             = Prop_rc_rudder;
      rc_switchA            = Prop_rc_switchA;
      rc_switchC            = Prop_rc_switchC;
      rc_valid              = Prop_rc_valid;
      temps_tache_prop      = Prop_control_time_us;

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

    // --- MODIF : Machine d'état ---
    Serial.print("[SM]       ");
    Serial.print(temps_tache_sm, 1);
    Serial.print(" us  |  Top=");
    Serial.print(topStateStr(sm_top));
    Serial.print("  Run=");
    Serial.print(runStateStr(sm_run));
    Serial.print("  Foils=");
    Serial.println(sm_foils ? "ON" : "OFF");

    // --- MODIF : Propulsion / manette ---
    Serial.print("[Prop]     ");
    Serial.print(temps_tache_prop, 1);
    Serial.print(" us  |  RC=");
    Serial.print(rc_valid ? "OK " : "LOST");
    Serial.print("  Thr=");
    Serial.print(rc_throttle, 2);
    Serial.print("  Rud=");
    Serial.print(rc_rudder, 2);
    Serial.print("  A=");
    Serial.print(rc_switchA ? 1 : 0);
    Serial.print("  C=");
    Serial.println(rc_switchC ? 1 : 0);

    // --- Temps tâche RPi ---
    float duree_us = (float)(micros() - t_debut);
    Serial.print("[RPi]      ");
    Serial.print(duree_us, 1);
    Serial.println(" us");
    Serial.println("------------------------------------------");

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_RPI_MS));
  }
}