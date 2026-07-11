// =====================================================
// Plateforme : Teensy 4.1
// FreeRTOS   : tsandmann/freertos-teensy
// Capteur    : IFM UGT207 (4-20mA) + résistance 150Ω
// IMU/GPS    : Xsens MTi-670 sur Serial5 (pins 21/20)
// Servo      : PWMServo sur pin 9
// RPi        : Serial actuel → Serial2 futur (pins 7/8)
//
// TESTS UNITAIRES : voir les flags TEST_XSENS / TEST_SONAR /
//   TEST_FOILS dans config.h. La tâche RPi reste toujours
//   active pour l'affichage Serial.
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "task_sonar.h"
#include "task_foils_control.h"
#include "task_rpi.h"
#include "task_xsens.h"

SemaphoreHandle_t dataMutex;

void setup()
{
  Serial.begin(9600);

  analogReadResolution(10);

  dataMutex = xSemaphoreCreateMutex();

#if RUN_XSENS
  xTaskCreate(Task_Xsens,          "Xsens", 2048, NULL, 2, NULL);
#endif
#if RUN_SONAR
  xTaskCreate(Task_LectureSonar,   "Sonar", 2048, NULL, 2, NULL);
#endif
#if RUN_FOILS
  xTaskCreate(Task_foils_Control,  "Foils", 2048, NULL, 3, NULL);
#endif
  xTaskCreate(Task_RPi,            "RPi",   2048, NULL, 1, NULL);  // toujours actif

  vTaskStartScheduler();
}

void loop() {}