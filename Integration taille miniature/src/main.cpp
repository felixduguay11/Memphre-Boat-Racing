// =====================================================
// Plateforme : Teensy 4.1
// FreeRTOS   : tsandmann/freertos-teensy
// Capteur    : IFM UGT207 (4-20mA) + résistance 150Ω
// IMU/GPS    : Xsens MTi-670 sur Serial5 (pins 21/20)
// Servo      : PWMServo
// RPi        : Serial actuel → Serial2 futur (pins 7/8)
//
// AJOUT : tâches Task_Propulsion (FlySky + 2 ESC + rudder) et
//         Task_StateMachine (machine d'état), portées du miniature.
//
// >>> FUSION : si ton main.cpp réel utilise les gardes #if RUN_XSENS /
//     RUN_SONAR / RUN_FOILS, garde-le tel quel et ajoute simplement
//     les 2 #include et les 2 xTaskCreate ci-dessous parmi les autres.
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "task_sonar.h"
#include "task_foils_control.h"
#include "task_rpi.h"
#include "task_xsens.h"
#include "task_state_machine.h"
#include "task_propulsion.h"

SemaphoreHandle_t dataMutex;

void setup()
{
  Serial.begin(9600);

  analogReadResolution(10);

  dataMutex = xSemaphoreCreateMutex();

  xTaskCreate(Task_StateMachine,   "StateM", STACK_SM,    NULL, PRIO_SM,    NULL);
  xTaskCreate(Task_Xsens,          "Xsens",  STACK_XSENS, NULL, PRIO_XSENS, NULL);
  xTaskCreate(Task_foils_Control,  "Foils",  STACK_FOILS, NULL, PRIO_FOILS, NULL);
  xTaskCreate(Task_LectureSonar,   "Sonar",  STACK_SONAR, NULL, PRIO_SONAR, NULL);
  xTaskCreate(Task_RPi,            "RPi",    STACK_RPI,   NULL, PRIO_RPI,   NULL);
  xTaskCreate(Task_Propulsion,     "Prop",   STACK_PROP,  NULL, PRIO_PROP,  NULL);

  vTaskStartScheduler();
}

void loop() {}