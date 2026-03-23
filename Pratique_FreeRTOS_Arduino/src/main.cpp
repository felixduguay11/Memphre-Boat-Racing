#include <Arduino.h>
#include <Servo.h>
#include <Wire.h>
#include <Arduino_FreeRTOS.h>
#include <task.h>
#include <semphr.h>

Servo myservo;

// Variables partagées
float distance = 0.0;
float output = 0.0;

// variables globales
float temps_capteurs;
float temps_calculs;
float temps_servos;

// Mutex pour protection
SemaphoreHandle_t dataMutex;
SemaphoreHandle_t serialMutex;

// Calculs
float distance_ref = 55;
float prev_target;
float error = 0;
float integral = 0;
float derivative = 0;
float Kp;
float Ki;
float Kd;

// Sonar
const int sonarPin = A0;
float alpha_sonar = 0.2;          // Filtrage
float voltage_0cm = 0.97;         // 0.97 = tension a distance de 0cm
float courant_min = 4.0;          // 4.0 = 4.0mA a distance de 0cm
float range_sonar = 200.0;        // 200 = Range du capteur (220cm - 20cm)
float range_non_detection = 20.0; // 20 = range de non detection du capteur
float tension_max = 5.0;          // 5.0 = tension maximale entrant (de la distance maximale)
float bits_adc = 1023.0;          // 1023 = nombre de bits du ADC

// =====================================================
// TACHE 1 : Lecture Sonar
// =====================================================
void Task_LectureSonar(void *ptr_sonar)
{
  TickType_t lastWakeTime_capteurs = xTaskGetTickCount();
  (void) ptr_sonar;

  while (1){

    int raw = analogRead(sonarPin);
    float voltage = raw * (tension_max / bits_adc);

    float newDistance = ((voltage - voltage_0cm) / courant_min) * range_sonar + range_non_detection; 
    float distance_filt = alpha_sonar * newDistance + (1 - alpha_sonar) * distance_filt;

    distance_filt = constrain(distance_filt, 20.0, 220.0);

    // Protection mutex
    if (xSemaphoreTake(dataMutex, portMAX_DELAY)){
      distance = distance_filt;
      xSemaphoreGive(dataMutex);
    }
    TickType_t time_capteurs = xTaskGetTickCount();
    temps_capteurs = time_capteurs - lastWakeTime_capteurs;

    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(10)))
    {
      Serial.print("sonar temps: ");
      Serial.println(temps_capteurs);
      Serial.print(" | Distance: ");
      Serial.print(distance);
      xSemaphoreGive(serialMutex);
    }

    vTaskDelayUntil(&lastWakeTime_capteurs, pdMS_TO_TICKS(200)); // 100ms = 0.1s = 10Hz
  }
}

// =====================================================
// TACHE 2 : Calculs (PID / Fuzzy / Filtre)
// =====================================================
void Task_Calculs(void *ptr_calculs){
  
  (void) ptr_calculs;
  TickType_t lastWakeTime_calculs = xTaskGetTickCount();
  TickType_t prevTick = xTaskGetTickCount();

  while (1){

    float local_Distance;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      local_Distance = distance;
      xSemaphoreGive(dataMutex);
    }

    // ---------- Erreur ----------
    error = distance_ref - local_Distance;
    float absE = abs(error);
    if (absE < 1) error = 0;

    // ---------- INTEGRALE ----------
    TickType_t now = xTaskGetTickCount();
    float dt = (now - prevTick) * portTICK_PERIOD_MS / 1000.0f;
    prevTick = now;
    if (dt <= 0.0001f) dt = 0.0001f;

    integral += error * dt;
    integral = constrain(integral, -20, 20);

    // ---------- DERIVEE ----------
    derivative = -(local_Distance - prev_target) / dt;
    prev_target = local_Distance;
    
    // ---------- FUZZY ----------
    if (absE > 10) {          // grosse erreur
        Kp = 1.6;             // 2.5
        Ki = 0.0;             // 0.0
        Kd = 1.9;             // 1.5
    }
    else if (absE > 5) {      // erreur moyenne
        Kp = 0.6;             // 1.5
        Ki = 0.05;            // 0.02
        Kd = 0.4;             // 0.8
    }
    else {                    // proche consigne
        Kp = 0.4;             // 0.6
        Ki = 0.1;            // 0.05
        Kd = 0.2;             // 0.3
    }

    // ---------- PID ----------
    float newOutput = Kp * error + Ki * integral + Kd * derivative;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      output = newOutput;
      xSemaphoreGive(dataMutex);
    }
    TickType_t time_calculs = xTaskGetTickCount();
    temps_calculs = time_calculs - lastWakeTime_calculs;

    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(5)))
    {
      Serial.print("calculs: ");
      Serial.println(temps_calculs);
      xSemaphoreGive(serialMutex);
    }

    vTaskDelayUntil(&lastWakeTime_calculs, pdMS_TO_TICKS(200)); // 100ms = 0.1s = 10Hz
  }
}

// =====================================================
// TACHE 3 : Commande Servo
// =====================================================
void Task_CommandeServo(void *ptr_commande)
{
  (void) ptr_commande;
  TickType_t lastWakeTime_servo = xTaskGetTickCount();

  float servo_cmd;
  float servoNeutral = 100;

  while (1)
  {
    float localOutput = 0;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      localOutput = output;
      xSemaphoreGive(dataMutex);
    }

    servo_cmd =  servoNeutral - localOutput;
    myservo.write(constrain(servo_cmd, 30, 170));

    TickType_t time_servo = xTaskGetTickCount();
    temps_servos = time_servo - lastWakeTime_servo;

    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(5)))
    {
      Serial.print("servo: ");
      Serial.println(temps_servos);
      xSemaphoreGive(serialMutex);
    }

    vTaskDelayUntil(&lastWakeTime_servo, pdMS_TO_TICKS(200)); // 100ms = 0.1s = 10Hz
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
    myservo.write(70);

    dataMutex = xSemaphoreCreateMutex();
    serialMutex = xSemaphoreCreateMutex();

    // Création tâches
    xTaskCreate(Task_LectureSonar, "Sonar", 256, NULL, 2, NULL);
    xTaskCreate(Task_Calculs, "Calculs", 256, NULL, 3, NULL);
    xTaskCreate(Task_CommandeServo, "Servo", 256, NULL, 1, NULL);

    // Démarrage scheduler
    vTaskStartScheduler();
}

void loop() {}