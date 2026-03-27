// ============================================================
//  main.cpp  –  RC Hydrofoil – Teensy 4.1
//  Phase 4: IMU + Sonar + RC + ESC bring-up
// ============================================================

#include <Arduino.h>
#include <Config.h>
#include <MPU6500.h>
#include <HCSR04.h>
#include <Receiver.h>
#include <ESC.h>

IMU        imu;
Sonar      sonar;
RCReceiver rc;
ESC        esc;

static uint32_t lastImuUpdate = 0;
static uint32_t lastSonar     = 0;
static uint32_t lastRC        = 0;
static uint32_t lastESC       = 0;
static uint32_t lastPrint     = 0;
static uint32_t lastBlink     = 0;

static constexpr uint32_t SONAR_PERIOD_MS = 1000UL / SONAR_UPDATE_HZ;
static constexpr uint32_t RC_PERIOD_MS    = 20;   // 50 Hz
static constexpr uint32_t ESC_PERIOD_MS   = 20;   // 50 Hz

void setup()
{
    Serial.begin(SERIAL_BAUD);
    while (!Serial && millis() < 3000) {}

    pinMode(LED_BUILTIN, OUTPUT);

    Serial.println("=== RC Hydrofoil – Phase 4 ===");

    if (!imu.begin(IMU_GYRO_RANGE, IMU_ACCEL_RANGE)) {
        Serial.println("[FATAL] IMU init failed.");
        while (true) { delay(500); }
    }
    imu.calibrate(IMU_CAL_SAMPLES);

    sonar.begin();
    sonar.trigger();
    lastSonar = millis();

    rc.begin();

    // ESC arming blocks for ESC_ARM_DELAY_MS — RC must be off or at min throttle
    esc.begin();

    Serial.println("[MAIN] Entering main loop");
}

void loop()
{
    uint32_t now_us = micros();
    uint32_t now_ms = millis();


    // // IMU at 500 Hz
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

    // ESC at 50 Hz — pass throttle directly from RC
    // SwC (switchC) enables reverse, otherwise forward only
    if ((now_ms - lastESC) >= ESC_PERIOD_MS) {
        lastESC = now_ms;

        if (rc.isValid()) {
            float throttle = rc.throttle();  // [0, +1]

            // Remap to forward [0, +1] or reverse [-1, 0] based on SwC
            if (rc.switchC()) {
                throttle = -throttle;  // reverse mode
            }

            esc.set(throttle);
        } else {
            // No RC signal — stop motors
            esc.stop();
        }
    }

    // Serial debug at 10 Hz
    if ((now_ms - lastPrint) >= DEBUG_PERIOD_MS) {
        lastPrint = now_ms;

        // const ImuData& d = imu.getData();
        // Serial.printf(
        //     "[IMU]   Roll: %6.1f deg  Pitch: %6.1f deg\n",
        //     d.roll_deg, d.pitch_deg
        // );

        // if (sonar.isValid()) {
        //     Serial.printf("[Sonar] Distance: %6.1f cm\n", sonar.getDistanceCm());
        // }

        if (rc.isValid()) {
            Serial.printf(
                "[RC]    Thr: %5.2f  Rud: %5.2f  SwA: %d  SwB: %d  SwC: %d\n",
                rc.throttle(), rc.rudder(),
                rc.switchA(), rc.switchB(), rc.switchC()
            );
        }

        Serial.printf("[ESC]   Throttle: %5.2f  Armed: %d\n",
                      esc.getThrottle(), esc.isArmed());
    }
}