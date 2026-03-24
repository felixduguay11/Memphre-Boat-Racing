// ============================================================
//  main.cpp  –  RC Hydrofoil – Teensy 4.1
//  Phase 2: IMU + Sonar bring-up
// ============================================================

#include <Arduino.h>
#include "Config.h"
#include "MPU6500.h"
#include "HCSR04.h"

IMU   imu;
Sonar sonar;

static uint32_t lastImuUpdate = 0;
static uint32_t lastSonar     = 0;
static uint32_t lastPrint     = 0;

static constexpr uint32_t SONAR_PERIOD_MS = 1000UL / SONAR_UPDATE_HZ;

void setup()
{
    Serial.begin(SERIAL_BAUD);
    while (!Serial && millis() < 3000) {}

    Serial.println("=== RC Hydrofoil – IMU + Sonar test ===");

    if (!imu.begin(IMU_GYRO_RANGE, IMU_ACCEL_RANGE)) {
        Serial.println("[FATAL] IMU init failed.");
        while (true) { delay(500); }
    }
    imu.calibrate(IMU_CAL_SAMPLES);

    sonar.begin();
    sonar.trigger();  // fire first pulse so update() has data on first cycle
    lastSonar = millis();

    Serial.println("[MAIN] Entering main loop");
}

void loop()
{
    uint32_t now_us = micros();
    uint32_t now_ms = millis();

    // IMU at 500 Hz
    if ((now_us - lastImuUpdate) >= IMU_PERIOD_US) {
        lastImuUpdate = now_us;
        imu.update();
    }

    // Sonar at 20 Hz — update() then trigger(), always in this order
    if ((now_ms - lastSonar) >= SONAR_PERIOD_MS) {
        lastSonar = now_ms;
        sonar.update();   // consume echo from previous trigger
        sonar.trigger();  // fire next pulse
    }

    // Serial debug at 10 Hz
    if ((now_ms - lastPrint) >= DEBUG_PERIOD_MS) {
        lastPrint = now_ms;

        const ImuData& d = imu.getData();
        Serial.printf(
            "[IMU]   Roll: %7.2f deg  Pitch: %7.2f deg  "
            "Ax: %6.3f g  Ay: %6.3f g  Az: %6.3f g  Temp: %.1f C\n",
            d.roll_deg, d.pitch_deg,
            d.accel_x_g, d.accel_y_g, d.accel_z_g, d.temp_c
        );

        if (sonar.isValid()) {
            Serial.printf("[Sonar] Distance: %6.1f cm  (raw: %6.1f cm)\n",
                          sonar.getDistanceCm(), sonar.getRawCm());
        } else {
            Serial.println("[Sonar] No valid reading");
        }
    }
}