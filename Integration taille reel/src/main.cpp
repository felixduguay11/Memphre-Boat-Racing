// =====================================================
// Plateforme : Teensy 4.1
// FreeRTOS   : tsandmann/freertos-teensy
// Capteur    : IFM UGT207 (4-20mA) + résistance 150Ω
// IMU/GPS    : Xsens MTi-670 sur Serial5 (pins 21/20)
// Servo      : PWMServo sur pin 9
// RPi        : Serial actuel → Serial2 futur (pins 7/8)
// =====================================================

#include <arduino_freertos.h>
#include <semphr.h>
#include "task_sonar.h"
#include "task_height_control.h"
#include "task_rpi.h"
#include "task_xsens.h"

// =====================================================
// Mutex partagé entre toutes les tâches
// =====================================================
SemaphoreHandle_t dataMutex;


// =====================================================
// SETUP
// =====================================================
///*
void setup()
{
  Serial.begin(9600);    // Serial Monitor USB

  // ADC 12 bits — obligatoire sur Teensy 4.1
  analogReadResolution(12);

  dataMutex = xSemaphoreCreateMutex();

  // Priorités :
  //   IMU=3, FoilsControl=3 — tâches critiques temps-réel
  //   Sonar=2
  //   RPi=1 — basse, n'interfère pas avec le contrôle
  xTaskCreate(Task_Xsens,           "Xsens", 2048, NULL, 2, NULL);
  xTaskCreate(Task_Height_Control,  "Foils", 2048, NULL, 3, NULL);
  xTaskCreate(Task_LectureSonar,    "Sonar", 2048, NULL, 2, NULL);
  xTaskCreate(Task_RPi,             "RPi",   2048, NULL, 1, NULL);

  vTaskStartScheduler();
}

void loop() {}
//*/
/*
void setup()
{
    Serial.begin(9600);     // USB monitor
    Serial5.begin(115200);

    delay(1000);
    Serial.println("=== RAW MTi-670 HEX STREAM ===");
}

void loop()
{
    while (Serial5.available())
    {
        uint8_t b = Serial5.read();

        // Affichage hex sur 2 digits
        if (b < 0x10) Serial.print("0");
        Serial.print(b, 16);
        Serial.print(" ");
    }
}
*/