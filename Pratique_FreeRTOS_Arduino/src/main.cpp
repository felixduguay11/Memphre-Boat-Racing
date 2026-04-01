#include <Arduino.h>
#include <Servo.h>
#include <Wire.h>
#include <Arduino_FreeRTOS.h>
#include <task.h>
#include <semphr.h>

// Variables partagées (protégées par dataMutex)
float distance = 0.0;
float output   = 0.0;
float cmd      = 0.0;

// Temps d'exécution en microsecondes (protégés par dataMutex)
volatile float temps_capteurs_us = 0.0;
volatile float temps_calculs_us  = 0.0;
volatile float temps_servos_us   = 0.0;

// Mutex
SemaphoreHandle_t dataMutex;

float distance_ref  = 70.0f;
float distance_filt = 70.0f;

// Calculs PID
float prev_target   = 0.0;
float error         = 0.0;
float integral      = 0.0;
float derivative    = 0.0;
float Kp, Ki, Kd;

// Sonar
const int sonarPin        = A0;
float alpha_sonar         = 0.2f;
float voltage_0cm         = 0.97f;
float range_sonar         = 200.0f;
float range_non_detection = 20.0f;
float tension_max         = 5.0f;
float bits_adc            = 1023.0f;

// Servo
Servo myservo;
float servo_cmd        = 0.0f;
float cmd_filt         = 0.0f;
float alpha_servo      = 0.2f;
const int servoNeutral = 148;
const int servo_min    = 120;
const int servo_max    = 163;


// =====================================================
// TACHE 1 : Lecture Sonar  (priorité 1)
// =====================================================
void Task_LectureSonar(void *ptr_sonar)
{
  (void) ptr_sonar;
  TickType_t lastWakeTime = xTaskGetTickCount();

  while (1)
  {
    uint16_t t_debut = TCNT1;  // timer hardware 16-bit, 62.5ns/tick à 16MHz

    int raw       = analogRead(sonarPin);
    float voltage = raw * (tension_max / bits_adc);

    float newDistance = ((voltage - voltage_0cm) / (tension_max - voltage_0cm)) * range_sonar + range_non_detection;
    distance_filt = alpha_sonar * newDistance + (1.0f - alpha_sonar) * distance_filt;
    distance_filt = constrain(distance_filt, 20.0f, 220.0f);

    uint16_t t_fin = TCNT1;
    float duree_us = (t_fin - t_debut) / 16.0f;  // en microsecondes

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      distance          = distance_filt;
      temps_capteurs_us = duree_us;
      xSemaphoreGive(dataMutex);
    }

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(10));
  }
}


// =====================================================
// TACHE 2 : Calculs PID Fuzzy  (priorité 3)
// =====================================================
void Task_Calculs(void *ptr_calculs)
{
  (void) ptr_calculs;
  TickType_t lastWakeTime = xTaskGetTickCount();
  TickType_t prevTick     = lastWakeTime;

  while (1)
  {
    uint16_t t_debut = TCNT1;

    float local_Distance;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      local_Distance = distance;
      xSemaphoreGive(dataMutex);
    }

    // --- Erreur ---
    error      = distance_ref - local_Distance;
    float absE = abs(error);
    if (absE < 1.0f) error = 0.0f;

    // --- dt ---
    TickType_t now = xTaskGetTickCount();
    float dt = (now - prevTick) * portTICK_PERIOD_MS / 1000.0f;
    prevTick = now;
    if (dt < 0.0001f) dt = 0.0001f;     // Protection pour la premiere iteration

    // --- Integrale ---
    integral += error * dt;
    integral = constrain(integral, -20.0f, 20.0f);

    // --- Derivee ---
    derivative  = -(local_Distance - prev_target) / dt;
    prev_target = local_Distance;

    // --- Fuzzy ---
    if (absE > 10.0f) {
      Kp = 1.6f;  Ki = 0.0f;   Kd = 1.9f;
    } else if (absE > 5.0f) {
      Kp = 0.6f;  Ki = 0.05f;  Kd = 0.4f;
    } else {
      Kp = 0.4f;  Ki = 0.1f;   Kd = 0.2f;
    }

    float newOutput = Kp * error + Ki * integral + Kd * derivative;

    uint16_t t_fin = TCNT1;
    float duree_us = (t_fin - t_debut) / 16.0f;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      output           = newOutput;
      temps_calculs_us = duree_us;
      xSemaphoreGive(dataMutex);
    }

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(50));
  }
}


// =====================================================
// TACHE 3 : Commande Servo  (priorité 2)
// =====================================================
void Task_CommandeServo(void *ptr_commande)
{
  (void) ptr_commande;
  TickType_t lastWakeTime = xTaskGetTickCount();

  while (1)
  {
    uint16_t t_debut = TCNT1;

    float localOutput;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      localOutput = output;
      xSemaphoreGive(dataMutex);
    }

    servo_cmd = servoNeutral + localOutput;
    cmd_filt  = alpha_servo * servo_cmd + (1.0f - alpha_servo) * cmd_filt;
    cmd       = constrain(cmd_filt, servo_min, servo_max);
    myservo.write((int)cmd);

    uint16_t t_fin = TCNT1;
    float duree_us = (t_fin - t_debut) / 16.0f;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      temps_servos_us = duree_us;
      xSemaphoreGive(dataMutex);
    }

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(50));
  }
}


// =====================================================
// TACHE 4 : Debug Serial  (priorité 1 — basse)
// Seule tâche à utiliser Serial → plus de contention
// =====================================================
void Task_Debug(void *ptr_debug)
{
  (void) ptr_debug;
  TickType_t lastWakeTime = xTaskGetTickCount();

  while (1)
  {
    uint16_t t_debut = TCNT1;

    float d, o, c, t_s, t_c, t_sv;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      d    = distance;
      o    = output;
      c    = cmd;
      t_s  = temps_capteurs_us;
      t_c  = temps_calculs_us;
      t_sv = temps_servos_us;
      xSemaphoreGive(dataMutex);
    }

    Serial.print(F("[Sonar]   "));
    Serial.print(t_s, 1);
    Serial.print(F(" us  |  Dist="));
    Serial.print(d, 1);
    Serial.println(F(" cm"));

    Serial.print(F("[Calculs] "));
    Serial.print(t_c, 1);
    Serial.print(F(" us  |  Erreur="));
    Serial.print(distance_ref - d, 2);
    Serial.print(F("  Output="));
    Serial.println(o, 2);

    Serial.print(F("[Servo]   "));
    Serial.print(t_sv, 1);
    Serial.print(F(" us  |  Cmd="));
    Serial.println(c, 1);

    uint16_t t_fin = TCNT1;
    float duree_us = (t_fin - t_debut) / 16.0f;

    Serial.print(F("[Prints]   "));
    Serial.print(duree_us, 1);
    Serial.print(F(" us  |"));

    Serial.println(F("-----------------------------"));

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(200)); // print toutes les 200ms
  }
}


// =====================================================
// SETUP
// =====================================================
void setup()
{
  Serial.begin(9600);
  Wire.begin();
  myservo.attach(9);
  myservo.write(120);

  // Timer 1 en mode normal pour TCNT1 (1 tick = 62.5ns à 16MHz)
  TCCR1A = 0x00;
  TCCR1B = 0x01; // prescaler = 1

  dataMutex = xSemaphoreCreateMutex();

  xTaskCreate(Task_LectureSonar,  "Sonar",   384, NULL, 1, NULL);
  xTaskCreate(Task_Calculs,       "Calculs", 384, NULL, 3, NULL);
  xTaskCreate(Task_CommandeServo, "Servo",   384, NULL, 2, NULL);
  xTaskCreate(Task_Debug,         "Debug",   384, NULL, 1, NULL);

  vTaskStartScheduler();
}

void loop() {}