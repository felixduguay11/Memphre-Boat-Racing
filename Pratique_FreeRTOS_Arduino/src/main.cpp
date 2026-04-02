#include <Arduino.h>
#include <Servo.h>
#include <Wire.h>
#include <Arduino_FreeRTOS.h>
#include <task.h>
#include <semphr.h>

// =====================================================
// CONSTANTES — 3 canaux
// =====================================================
#define NB_CANAUX 3

const int sonarPins[NB_CANAUX] = { A0, A1, A2 };   // indices 0, 1, 2
const int servoPins[NB_CANAUX] = { 9, 10, 11 };    // indices 0, 1, 2

// =====================================================
// VARIABLES PARTAGÉES (protégées par dataMutex)
// =====================================================
float distances[NB_CANAUX]       = { 0.0f, 0.0f, 0.0f };
float outputs[NB_CANAUX]         = { 0.0f, 0.0f, 0.0f };
float cmds[NB_CANAUX]            = { 0.0f, 0.0f, 0.0f };

volatile float temps_sonar_us    = 0.0f;
volatile float temps_controle_us = 0.0f;

SemaphoreHandle_t dataMutex;

// =====================================================
// PARAMÈTRES SONAR
// =====================================================
float alpha_sonar         = 0.2f;
float voltage_0cm         = 0.97f;
float range_sonar         = 200.0f;
float range_non_detection = 20.0f;
float tension_max         = 5.0f;
float bits_adc            = 1023.0f;

// Filtres sonar séparés pour chaque tâche — indices 0 à 2
float distance_filt_sonar[NB_CANAUX]    = { 70.0f, 70.0f, 70.0f };
float distance_filt_controle[NB_CANAUX] = { 70.0f, 70.0f, 70.0f };

// =====================================================
// PARAMÈTRES SERVO
// =====================================================
Servo servos[NB_CANAUX];
float alpha_servo      = 0.2f;
const int servoNeutral = 148;
const int servo_min    = 120;
const int servo_max    = 163;

float cmd_filt[NB_CANAUX] = { 148.0f, 148.0f, 148.0f };

// =====================================================
// PARAMÈTRES PID — un jeu COMPLET par canal (indices 0 à 2)
// =====================================================
float distance_ref[NB_CANAUX] = { 70.0f, 70.0f, 70.0f }; // consigne par canal
float prev_target[NB_CANAUX]  = { 70.0f, 70.0f, 70.0f }; // mémoire dérivée par canal
float integral_pid[NB_CANAUX] = {  0.0f,  0.0f,  0.0f }; // intégrale par canal
float deriv_filt[NB_CANAUX]   = {  0.0f,  0.0f,  0.0f }; // filtre dérivée par canal

const float alpha_derivee = 0.15f; // lissage de la dérivée (0.1 = très lisse)
const float zone_morte_deriv = 0.3f; // variation minimale en cm pour calculer la dérivée


// =====================================================
// Utilitaire : lecture + filtre d'un sonar
// =====================================================
float lire_sonar(int pin, float &filt)
{
  int raw       = analogRead(pin);
  float voltage = raw * (tension_max / bits_adc);
  float newDist = ((voltage - voltage_0cm) / (tension_max - voltage_0cm))
                  * range_sonar + range_non_detection;
  filt = alpha_sonar * newDist + (1.0f - alpha_sonar) * filt;
  filt = constrain(filt, 20.0f, 220.0f);
  return filt;
}

// =====================================================
// Utilitaire : PID Fuzzy pour le canal i (0, 1 ou 2)
// =====================================================
float calcul_pid(int i, float dist, float dt)
{
  float error = distance_ref[i] - dist;
  float absE  = abs(error);
  if (absE < 1.0f) error = 0.0f;

  // Intégrale — propre au canal i
  integral_pid[i] += error * dt;
  integral_pid[i]  = constrain(integral_pid[i], -20.0f, 20.0f);

  // Dérivée filtrée — zone morte pour ignorer le bruit résiduel
  float delta_dist = dist - prev_target[i];
  float raw_deriv  = (abs(delta_dist) > zone_morte_deriv) ? (-delta_dist / dt) : 0.0f;
  deriv_filt[i]    = alpha_derivee * raw_deriv + (1.0f - alpha_derivee) * deriv_filt[i];
  prev_target[i]   = dist;

  // Gains Fuzzy
  float Kp, Ki, Kd;
  if (absE > 10.0f) {
    Kp = 1.6f;  Ki = 0.0f;   Kd = 1.9f;
  } else if (absE > 5.0f) {
    Kp = 0.6f;  Ki = 0.05f;  Kd = 0.4f;
  } else {
    Kp = 0.4f;  Ki = 0.1f;   Kd = 0.2f;
  }

  return Kp * error + Ki * integral_pid[i] + Kd * deriv_filt[i];
}


// =====================================================
// TACHE 1 : Lecture des 3 Sonars  (priorité 2)
// =====================================================
void Task_LectureSonars(void *ptr)
{
  (void) ptr;
  TickType_t lastWakeTime = xTaskGetTickCount();

  while (1)
  {
    uint16_t t_debut = TCNT1;

    float local_dist[NB_CANAUX];
    for (int i = 0; i < NB_CANAUX; i++)          // i = 0, 1, 2
      local_dist[i] = lire_sonar(sonarPins[i], distance_filt_sonar[i]);

    uint16_t t_fin = TCNT1;
    float duree_us = (t_fin - t_debut) / 16.0f;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      for (int i = 0; i < NB_CANAUX; i++)
        distances[i] = local_dist[i];
      temps_sonar_us = duree_us;
      xSemaphoreGive(dataMutex);
    }

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(20));
  }
}


// =====================================================
// TACHE 2 : Boucle de contrôle complète  (priorité 3)
// =====================================================
void Task_Controle(void *ptr)
{
  (void) ptr;
  TickType_t lastWakeTime = xTaskGetTickCount();
  TickType_t prevTick     = lastWakeTime;
  float dist[NB_CANAUX];

  while (1)
  {
    uint16_t t_debut = TCNT1;
    
    float local_out[NB_CANAUX];
    float local_cmd[NB_CANAUX];

    // Lecture sonar avec filtre propre à cette tâche
    dist[0] = lire_sonar(sonarPins[0], distance_filt_controle[0]);
    dist[1] = lire_sonar(sonarPins[1], distance_filt_controle[1]);
    dist[2] = lire_sonar(sonarPins[2], distance_filt_controle[2]);

    // dt
    TickType_t now = xTaskGetTickCount();
    float dt = (now - prevTick) * portTICK_PERIOD_MS / 1000.0f;
    prevTick = now;
    if (dt < 0.0001f) dt = 0.0001f;

    for (int i = 0; i < NB_CANAUX; i++)         
    {
      // PID Fuzzy avec index i ← CORRIGÉ (variables séparées par canal)
      local_out[i] = calcul_pid(i, dist[i], dt);
    }

    for (int i = 0; i < NB_CANAUX; i++)
    {
      // Filtre + contrainte servo
      float servo_raw = servoNeutral + local_out[i];
      cmd_filt[i]     = alpha_servo * servo_raw + (1.0f - alpha_servo) * cmd_filt[i];
      local_cmd[i]    = constrain(cmd_filt[i], (float)servo_min, (float)servo_max);
    }
    servos[0].write((int)local_cmd[0]);
    servos[1].write((int)local_cmd[1]);
    servos[2].write((int)local_cmd[2]);


    uint16_t t_fin = TCNT1;
    float duree_us = (t_fin - t_debut) / 16.0f;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      for (int i = 0; i < NB_CANAUX; i++)
      {
        outputs[i] = local_out[i];
        cmds[i]    = local_cmd[i];
      }
      temps_controle_us = duree_us;
      xSemaphoreGive(dataMutex);
    }

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(20));
  }
}


// =====================================================
// TACHE 3 : Affichage Serial  (priorité 1 — basse)
// =====================================================
void Task_Debug(void *ptr)
{
  (void) ptr;
  TickType_t lastWakeTime = xTaskGetTickCount();

  while (1)
  {
    uint16_t t_debut = TCNT1;

    float d[NB_CANAUX], o[NB_CANAUX], c[NB_CANAUX];
    float t_s, t_ctrl;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      for (int i = 0; i < NB_CANAUX; i++)
      {
        d[i] = distances[i];
        o[i] = outputs[i];
        c[i] = cmds[i];
      }
      t_s    = temps_sonar_us;
      t_ctrl = temps_controle_us;
      xSemaphoreGive(dataMutex);
    }

    Serial.print(F("[Sonars]   "));
    Serial.print(t_s, 1);
    Serial.print(F(" us  |  "));
    for (int i = 0; i < NB_CANAUX; i++)
    {
      Serial.print(F("S")); Serial.print(i + 1);
      Serial.print(F("="));  Serial.print(d[i], 1);
      Serial.print(F("cm  "));
    }
    Serial.println();

    Serial.print(F("[Controle] "));
    Serial.print(t_ctrl, 1);
    Serial.print(F(" us  |  "));
    for (int i = 0; i < NB_CANAUX; i++)
    {
      Serial.print(F("O")); Serial.print(i + 1);
      Serial.print(F("="));  Serial.print(o[i], 1);
      Serial.print(F("  "));
    }
    Serial.println();

    Serial.print(F("[Servos]           |  "));
    for (int i = 0; i < NB_CANAUX; i++)
    {
      Serial.print(F("C")); Serial.print(i + 1);
      Serial.print(F("="));  Serial.print(c[i], 1);
      Serial.print(F("  "));
    }
    Serial.println();

    uint16_t t_fin = TCNT1;
    float duree_us = (t_fin - t_debut) / 16.0f;

    Serial.print(F("[Debug]    "));
    Serial.print(duree_us, 1);
    Serial.println(F(" us"));
    Serial.println(F("------------------------------------------"));

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(500));
  }
}


// =====================================================
// SETUP
// =====================================================
void setup()
{
  Serial.begin(9600);
  Wire.begin();

  for (int i = 0; i < NB_CANAUX; i++)
  {
    servos[i].attach(servoPins[i]);
    servos[i].write(servo_min);
  }

  TCCR1A = 0x00;
  TCCR1B = 0x01;

  dataMutex = xSemaphoreCreateMutex();

  xTaskCreate(Task_LectureSonars, "Sonars",   512, NULL, 2, NULL);
  xTaskCreate(Task_Controle,      "Controle", 512, NULL, 3, NULL);
  xTaskCreate(Task_Debug,         "Debug",    512, NULL, 1, NULL);

  vTaskStartScheduler();
}

void loop() {}