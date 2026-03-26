#pragma once

// ============================================================
//  Config.h  –  Central configuration for RC Hydrofoil
//  Teensy 4.1
//
//  Edit this file only. All components read their settings
//  from here. No magic numbers anywhere else.
// ============================================================


// ----------------------------------------------------------------
// ■  PIN MAP
// ----------------------------------------------------------------

// --- SPI bus (hardware SPI, shared) ---
// MOSI → 11  |  MISO → 12  |  SCK → 13  (Teensy 4.1 default)
#define PIN_IMU_CS          10

// --- Sonar HC-SR04 ---
#define PIN_SONAR_TRIG      14
#define PIN_SONAR_ECHO      15

// --- ESC (PWM) ---
#define PIN_ESC_LEFT        2
#define PIN_ESC_RIGHT       3

// --- Servo: direction (rudder) ---
#define PIN_SERVO_RUDDER    4

// --- Servos: hydrofoils ---
#define PIN_FOIL_FRONT      5
#define PIN_FOIL_REAR_LEFT  6
#define PIN_FOIL_REAR_RIGHT 7

// --- FlySky receiver (PWM input, 5 channels used) ---
#define PIN_RC_CH1          20   // Rudder
#define PIN_RC_CH3          22   // Throttle
#define PIN_RC_CH5          24   // Switch A
#define PIN_RC_CH6          25   // Switch B
#define PIN_RC_CH7          26   // Switch C


// ----------------------------------------------------------------
// ■  IMU  (MPU6500 via SPI)
// ----------------------------------------------------------------
#define IMU_SPI_SPEED       8000000UL   // 8 MHz (max 20 MHz)

// Gyro full-scale range: DPS_250 | DPS_500 | DPS_1000 | DPS_2000
#define IMU_GYRO_RANGE      GyroRange::DPS_500

// Accel full-scale range: G2 | G4 | G8 | G16
#define IMU_ACCEL_RANGE     AccelRange::G4

// Complementary filter: 0 = pure gyro, 1 = pure accel
#define IMU_ALPHA           0.02f

// Static calibration sample count (sensor must be still)
#define IMU_CAL_SAMPLES     500

// Update rate
#define IMU_UPDATE_HZ       500
#define IMU_PERIOD_US       (1000000UL / IMU_UPDATE_HZ)


// ----------------------------------------------------------------
// ■  SONAR  (HC-SR04)
// ----------------------------------------------------------------
#define SONAR_UPDATE_HZ     20           // [Hz]  max ~25 Hz reliable
#define SONAR_PERIOD_US     (1000000UL / SONAR_UPDATE_HZ)
#define SONAR_MIN_CM        2.0f         // Below = invalid
#define SONAR_MAX_CM        400.0f       // Above = invalid
#define SONAR_FILTER_ALPHA  0.15f        // Low-pass: 0=frozen, 1=raw


// ----------------------------------------------------------------
// ■  ESC / MOTORS
// ----------------------------------------------------------------
#define ESC_PWM_FREQ_HZ      50          // Standard 50 Hz ESC signal
#define ESC_PULSE_MIN_US     1000        // Full reverse / disarmed
#define ESC_PULSE_NEUTRAL_US 1500        // Stop
#define ESC_PULSE_MAX_US     2000        // Full forward
#define ESC_ARM_PULSE_US     ESC_PULSE_MIN_US
#define ESC_ARM_DELAY_MS     2000        // Hold low pulse to arm


// ----------------------------------------------------------------
// ■  SERVOS
// ----------------------------------------------------------------
// Rudder (direction servo)
#define RUDDER_PWM_FREQ_HZ   50
#define RUDDER_PULSE_MIN_US  500
#define RUDDER_PULSE_MAX_US  2500
#define RUDDER_CENTER_US     1500
#define RUDDER_MIN_DEG       0
#define RUDDER_MAX_DEG       180

// Hydrofoil servos (same spec for all three)
#define FOIL_PWM_FREQ_HZ     50
#define FOIL_PULSE_MIN_US    500
#define FOIL_PULSE_MAX_US    2500
#define FOIL_CENTER_US       1500        // Foil flat/neutral
#define FOIL_MIN_DEG         0
#define FOIL_MAX_DEG         180

// Physical travel limits (tune after installation)
#define FOIL_ANGLE_MIN_DEG   60.0f       // Full dive
#define FOIL_ANGLE_MAX_DEG   120.0f      // Full lift
#define FOIL_ANGLE_NEUTRAL   90.0f       // Flat


// ----------------------------------------------------------------
// ■  RC RECEIVER  (FlySky, PWM — 5 channels)
// ----------------------------------------------------------------
#define RC_PULSE_MIN_US      1000
#define RC_PULSE_MAX_US      2000
#define RC_PULSE_MID_US      1500
#define RC_DEADBAND_US       30          // ±30 µs around centre
#define RC_TIMEOUT_MS        500         // No pulse → failsafe


// ----------------------------------------------------------------
// ■  STATE MACHINE
// ----------------------------------------------------------------
// Speed thresholds for Run sub-state transitions
// Expressed as fraction of max throttle (0.0 – 1.0)
#define SM_SPEED_THRESHOLD_HIGH   0.25f  // Avance → Contrôle
#define SM_SPEED_THRESHOLD_LOW    0.185f // Contrôle → Avance


// ----------------------------------------------------------------
// ■  HEIGHT CONTROLLER  (active in CONTROLE state)
// ----------------------------------------------------------------
#define HEIGHT_SETPOINT_DEFAULT_CM  10.0f
#define HEIGHT_SETPOINT_MIN_CM       5.0f
#define HEIGHT_SETPOINT_MAX_CM      25.0f

#define HEIGHT_KP    2.0f
#define HEIGHT_KI    0.1f
#define HEIGHT_KD    0.5f
#define HEIGHT_I_MAX 20.0f              // Anti-windup clamp [deg]


// ----------------------------------------------------------------
// ■  ROLL CONTROLLER  (active in CONTROLE state)
// ----------------------------------------------------------------
#define ROLL_SETPOINT_DEG   0.0f        // Keep wings level

#define ROLL_KP    3.0f
#define ROLL_KI    0.05f
#define ROLL_KD    0.8f
#define ROLL_I_MAX 15.0f                // Anti-windup clamp [deg]


// ----------------------------------------------------------------
// ■  SERIAL DEBUG
// ----------------------------------------------------------------
#define SERIAL_BAUD      115200
#define DEBUG_PRINT_HZ   10             // [Hz]
#define DEBUG_PERIOD_MS  (1000UL / DEBUG_PRINT_HZ)