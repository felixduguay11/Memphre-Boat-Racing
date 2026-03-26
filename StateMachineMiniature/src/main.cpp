// ============================================================
//  main.cpp  –  RC Hydrofoil – Teensy 4.1
//  Phase 3: IMU + Sonar + RC Receiver bring-up
// ============================================================

#include <Arduino.h>
#include "Config.h"
#include "MPU6500.h"
#include "HCSR04.h"
#include "Receiver.h"

IMU        imu;
Sonar      sonar;
RCReceiver rc;

static uint32_t lastImuUpdate = 0;
static uint32_t lastSonar     = 0;
static uint32_t lastRC        = 0;
static uint32_t lastPrint     = 0;

static constexpr uint32_t SONAR_PERIOD_MS = 1000UL / SONAR_UPDATE_HZ;
static constexpr uint32_t RC_PERIOD_MS    = 20;  // 50 Hz

void setup()
{
    Serial.begin(SERIAL_BAUD);
    while (!Serial && millis() < 3000) {}

    Serial.println("=== RC Hydrofoil – IMU + Sonar + RC test ===");

    // if (!imu.begin(IMU_GYRO_RANGE, IMU_ACCEL_RANGE)) {
    //     Serial.println("[FATAL] IMU init failed.");
    //     while (true) { delay(500); }
    // }
    // imu.calibrate(IMU_CAL_SAMPLES);

    // sonar.begin();
    // sonar.trigger();
    lastSonar = millis();

    rc.begin();

    Serial.println("[MAIN] Entering main loop");
}

void loop()
{
    uint32_t now_us = micros();
    uint32_t now_ms = millis();

    // IMU at 500 Hz
    // if ((now_us - lastImuUpdate) >= IMU_PERIOD_US) {
    //     lastImuUpdate = now_us;
    //     imu.update();
    // }

    // Sonar at 20 Hz
    // if ((now_ms - lastSonar) >= SONAR_PERIOD_MS) {
    //     lastSonar = now_ms;
    //     sonar.update();
    //     sonar.trigger();
    // }

    // RC at 50 Hz
    if ((now_ms - lastRC) >= RC_PERIOD_MS) {
        lastRC = now_ms;
        rc.update();
    }

    // Serial debug at 10 Hz
    if ((now_ms - lastPrint) >= DEBUG_PERIOD_MS) {
        lastPrint = now_ms;

        // const ImuData& d = imu.getData();
        // Serial.printf(
        //     "[IMU]   Roll: %6.1f deg  Pitch: %6.1f deg  Temp: %.1f C\n",
        //     d.roll_deg, d.pitch_deg, d.temp_c
        // );

        // if (sonar.isValid()) {
        //     Serial.printf("[Sonar] Distance: %6.1f cm\n", sonar.getDistanceCm());
        // } else {
        //     Serial.println("[Sonar] No valid reading");
        // }

        if (rc.isValid()) {
            Serial.printf(
                "[RC]    Thr: %5.2f  Rud: %5.2f  SwA: %d  SwB: %d  SwC: %d\n",
                rc.throttle(), rc.rudder(),
                rc.switchA(), rc.switchB(), rc.switchC()
            );
        } else {
            Serial.println("[RC]    No signal / timeout");
        }
    }
}