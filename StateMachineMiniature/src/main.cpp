// Memphre Boat Racing

#include <Arduino.h>
#include <Config.h>
#include <MPU6500.h>
#include <HCSR04.h>
#include <Receiver.h>
#include <ESC.h>
#include <ServoMoteur.h>
#include <StateMachine.h>
#include <HeightController.h>
#include <RollController.h>

IMU              imu;
Sonar            sonar;
RCReceiver       rc;
ESC              esc;
ServoRudder      rudder;
ServoFoils       foils;
StateMachine     sm;
HeightController heightCtrl;
RollController   rollCtrl;

static uint32_t lastImuUpdate = 0;
static uint32_t lastSonar     = 0;
static uint32_t lastRC        = 0;
static uint32_t lastControl   = 0;
static uint32_t lastPrint     = 0;
static uint32_t lastBlink     = 0;

static constexpr uint32_t SONAR_PERIOD_MS   = 1000UL / SONAR_UPDATE_HZ;
static constexpr uint32_t RC_PERIOD_MS      = 20;
static constexpr uint32_t CONTROL_PERIOD_MS = 50;   

// Track previous CONTROLE state to reset PIDs on entry
static bool wasControl = false;

void setup()
{
    Serial.begin(SERIAL_BAUD);
    while (!Serial && millis() < 3000) {}

    pinMode(LED_BUILTIN, OUTPUT);

    Serial.println("=== RC Hydrofoil – Phase 7 ===");

    if (!imu.begin(IMU_GYRO_RANGE, IMU_ACCEL_RANGE)) {
        Serial.println("[FATAL] IMU init failed.");
        while (true) { delay(500); }
    }
    imu.calibrate(IMU_CAL_SAMPLES);

    sonar.begin();
    sonar.trigger();
    lastSonar = millis();

    rc.begin();
    esc.begin();
    rudder.begin();
    foils.begin();
    sm.begin();
    heightCtrl.begin();
    rollCtrl.begin();

    Serial.println("[MAIN] Entering main loop");
}

void loop()
{
    uint32_t now_us = micros();
    uint32_t now_ms = millis();

    // Heartbeat LED — fast in RUN, slow in IDLE/STOP
    uint32_t blinkRate = sm.isRunning() ? 250 : 1000;
    if ((now_ms - lastBlink) >= blinkRate) {
        lastBlink = now_ms;
        digitalToggle(LED_BUILTIN);
    }

    // IMU at 500 Hz
    if ((now_us - lastImuUpdate) >= IMU_PERIOD_US) {
        lastImuUpdate = now_us;
        imu.update();
    }

    // Sonar at 20 Hz
    if ((now_ms - lastSonar) >= SONAR_PERIOD_MS) {
        lastSonar = now_ms;
        sonar.update();
        sonar.trigger();
    }

    // RC at 50 Hz
    if ((now_ms - lastRC) >= RC_PERIOD_MS) {
        lastRC = now_ms;
        rc.update();
    }

    // State machine + controllers + actuators at 50 Hz
    if ((now_ms - lastControl) >= CONTROL_PERIOD_MS) {
        float dt = CONTROL_PERIOD_MS / 1000.0f;   // [s]
        lastControl = now_ms;

        // Build RC input snapshot
        SMInputs in;
        in.throttle = rc.throttle();
        in.rudder   = rc.rudder();
        in.switchA  = rc.switchA();
        in.switchB  = false;  // unused
        in.switchC  = rc.switchC();
        in.rcValid  = rc.isValid();

        // Update state machine
        sm.update(in);

        // Reset PIDs on entry into CONTROLE
        bool nowControl = sm.isControl();
        if (nowControl && !wasControl) {
            heightCtrl.reset();
            rollCtrl.reset();
        }
        wasControl = nowControl;

        // Drive actuators
        switch (sm.getTopState()) {

            case TopState::IDLE:
            case TopState::STOP:
                esc.stop();
                rudder.center();
                foils.neutral();
                break;

            case TopState::RUN:
                rudder.set(in.rudder);

                switch (sm.getRunState()) {

                    case RunState::NEUTRE:
                        esc.stop();
                        foils.neutral();
                        break;

                    case RunState::AVANCE:
                        esc.set(in.throttle);
                        foils.neutral();
                        break;

                    case RunState::RECULE:
                        esc.set(-in.throttle);
                        foils.neutral();
                        break;

                    case RunState::CONTROLE: {
                        esc.set(in.throttle);

                        // PID height — same correction on all 3 foils
                        float heightOut = heightCtrl.update(
                            sonar.getDistanceCm(),
                            rc.knobHeight(),
                            dt
                        );

                        // PID roll — differential on rear foils
                        float rollOut = rollCtrl.update(
                            imu.getRoll(),
                            dt
                        );

                        // Mix: height lifts all, roll tilts rear
                        float front     = FOIL_ANGLE_NEUTRAL - heightOut;
                        float rearLeft  = FOIL_ANGLE_NEUTRAL - heightOut + rollOut; // 
                        float rearRight = FOIL_ANGLE_NEUTRAL - heightOut - rollOut; //

                        foils.set(front, rearLeft, rearRight);
                        break;
                    }
                }
                break;
        }
    }

    // Serial debug at 10 Hz
    if ((now_ms - lastPrint) >= DEBUG_PERIOD_MS) {
        lastPrint = now_ms;
        // sm.printDebug();
        imu.printDebug();
        // sonar.printDebug();
        // rc.printDebug();
        // esc.printDebug();
        // rudder.printDebug();
        foils.printDebug();
        if (sm.isControl()) {
            // heightCtrl.printDebug();
            rollCtrl.printDebug();
        }
        Serial.println("---");
    }
}