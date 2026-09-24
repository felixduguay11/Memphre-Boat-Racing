#include <Arduino.h>

#include <arduino_freertos.h>
#include <task.h>

void task1(void* pvParameters) {
    while (true) {
        Serial.println("Task 1 — 500ms");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void task2(void* pvParameters) {
    while (true) {
        Serial.println("Task 2 — 1200ms");
        vTaskDelay(pdMS_TO_TICKS(1200));
    }
}

void setup() {
    Serial.begin(115200);
    xTaskCreate(task1, "T1", 256, nullptr, 1, nullptr);
    xTaskCreate(task2, "T2", 256, nullptr, 1, nullptr);
    vTaskStartScheduler();
}

void loop() {}