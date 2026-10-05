// =====================================================
// Plateforme : Teensy 4.1
// FreeRTOS   : tsandmann/freertos-teensy
// Capteur    : IFM UGT207 (4-20mA) + résistance 150Ω
// IMU/GPS    : Xsens MTi-670 sur Serial5 (pins 21/20)
// Servo      : PWMServo (pins 2/3/4)
// VESC       : CAN3 (pins 30/31), IDs 10 et 11
// RPi        : USB (Serial = /dev/ttyACM0 côté Pi), JSON
//
// Architecture : voir Architecture2.png et include/rtos_com.h
// Option foils : FOILS_ACTIFS dans config.h
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "rtos_com.h"
#include "task_sonar.h"
#include "task_foils_control.h"
#include "task_rpi.h"
#include "task_xsens.h"
#include "task_pilote.h"
#include "task_state_machine.h"
#include "task_propulsion.h"
#include "task_esc_data.h"
#include "task_watchdog.h"

SemaphoreHandle_t  dataMutex;
SemaphoreHandle_t  adcMutex;
QueueHandle_t      qEvents;
QueueHandle_t      qCmd;
EventGroupHandle_t egEtat;

void setup()
{
  Serial.begin(BAUD_USB);             // USB CDC : le débit est ignoré

  // ADC 10 bits partout (levier + sonars)
  analogReadResolution(ADC_RESOLUTION);

  dataMutex = xSemaphoreCreateMutex();
  adcMutex  = xSemaphoreCreateMutex();
  qEvents   = xQueueCreate(FSM_QUEUE_LEN, sizeof(FsmEvent));
  qCmd      = xQueueCreate(1, sizeof(FsmCmd));        // mailbox
  egEtat    = xEventGroupCreate();

  ESC_InitCAN();                      // CAN3 250 kbps (Real_V1)

  xTaskCreate(Task_Watchdog,      "WD",     STACK_WD,     NULL, PRIO_WD,     &WD_handles[T_WD]);
  xTaskCreate(Task_StateMachine,  "FSM",    STACK_FSM,    NULL, PRIO_FSM,    &WD_handles[T_FSM]);
  xTaskCreate(Task_Propulsion,    "Prop",   STACK_PROP,   NULL, PRIO_PROP,   &WD_handles[T_PROP]);
  xTaskCreate(Task_Pilote,        "Pilote", STACK_PILOTE, NULL, PRIO_PILOTE, &WD_handles[T_PILOTE]);
  xTaskCreate(Task_ESC_Data,      "ESC",    STACK_ESC,    NULL, PRIO_ESC,    &WD_handles[T_ESC]);
  xTaskCreate(Task_Xsens,         "Xsens",  STACK_IMU,    NULL, PRIO_IMU,    &WD_handles[T_XSENS]);
#if FOILS_ACTIFS
  xTaskCreate(Task_foils_Control, "Foils",  STACK_FOILS,  NULL, PRIO_FOILS,  &WD_handles[T_FOILS]);
  xTaskCreate(Task_LectureSonar,  "Sonar",  STACK_SONAR,  NULL, PRIO_SONAR,  &WD_handles[T_SONAR]);
#endif
  xTaskCreate(Task_RPi,           "RPi",    STACK_RPI,    NULL, PRIO_RPI,    &WD_handles[T_RPI]);

  vTaskStartScheduler();
}

void loop() {}
